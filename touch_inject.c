/*
 * touch_inject.c — 内核级触摸注入模块
 *
 * 不注册虚拟 input 设备，直接向 input 子系统注入触摸事件。
 * 通过 /dev/touch_inject 字符设备提供 ioctl 接口。
 *
 * 目标: 6.1.128-android14-2025-03-ReSukiSU (aarch64 GKI)
 * 验证: 5.15 (x86) 语法兼容
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/string.h>
#include <linux/kprobes.h>
#include <linux/kallsyms.h>
#include <linux/utsname.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define DRV_NAME   "touch_inject"
#define DRV_CLASS  "touch_inject"

/* ---- ioctl 定义 ---- */
#define TI_IOC  'T'

struct ti_point {
    __s32 x, y, pressure, touch_major, touch_minor;
};
struct ti_cmd {
    __u32 cmd;
    struct ti_point pt;
    __u32 slot, tracking_id;
};

/* ARM64 _IOW 需要 _IOC_TYPECHECK，改用 _IOC */
#define _TI_CMD_IOC(nr) _IOC(_IOC_WRITE, TI_IOC, nr, sizeof(struct ti_cmd))
#define TI_DOWN  _TI_CMD_IOC(0x01)
#define TI_UP    _TI_CMD_IOC(0x02)
#define TI_MOVE  _TI_CMD_IOC(0x03)
#define TI_SYNC  _TI_CMD_IOC(0x04)
#define TI_SLOT  _TI_CMD_IOC(0x05)
#define TI_MAX   0x05

static char *touch_name = "";
module_param(touch_name, charp, 0444);

/* ---- 全局 ---- */
static struct {
    dev_t devno;
    struct cdev cdev;
    struct class *cls;
    struct device *dev;
    struct input_dev *touch;
    bool found;
} G;

/* ---- 触摸屏查找 ---- */
static struct input_dev *kprobe_hit;

static int cb_match(struct device *d, void *data)
{
    struct input_dev **out = data;
    struct input_dev *id = dev_get_drvdata(d);

    if (!id || !test_bit(EV_ABS, id->evbit) ||
        !test_bit(ABS_MT_POSITION_X, id->absbit) ||
        !test_bit(ABS_MT_POSITION_Y, id->absbit))
        return 0;

    pr_info(DRV_NAME": candidate '%s' %04x:%04x\n",
            id->name ?: "?", id->id.vendor, id->id.product);

    if (touch_name[0] && id->name && strstr(id->name, touch_name)) {
        *out = id;
        return 1;
    }
    if (!*out)
        *out = id;
    return 0;
}

/* kprobe: 捕获 input_register_device (第一个参数在 x0) */
static int kr_pre(struct kprobe *p, struct pt_regs *r)
{
#if defined(CONFIG_ARM64)
    struct input_dev *d = (struct input_dev *)r->regs[0];
#elif defined(CONFIG_X86_64)
    struct input_dev *d = (struct input_dev *)r->di;
#else
    struct input_dev *d = NULL;
#endif
    if (!d) return 0;
    if (test_bit(EV_ABS, d->evbit) &&
        test_bit(ABS_MT_POSITION_X, d->absbit) &&
        test_bit(ABS_MT_POSITION_Y, d->absbit)) {
        if (!kprobe_hit) kprobe_hit = d;
        if (touch_name[0] && d->name && strstr(d->name, touch_name))
            kprobe_hit = d;
    }
    return 0;
}

static struct kprobe kr = {
    .symbol_name = "input_register_device",
    .pre_handler = kr_pre,
};

static int find_ts(void)
{
    struct input_dev *f = NULL;
    int ret;

    /* input_class 未 EXPORT_SYMBOL, 通过 kallsyms 运行时查找 */
    {
        struct class *icls = (struct class *)kallsyms_lookup_name("input_class");
        if (icls)
            class_for_each_device(icls, NULL, &f, cb_match);
        else
            pr_warn(DRV_NAME": input_class not found via kallsyms\n");
    }

    ret = register_kprobe(&kr);
    if (ret) pr_warn(DRV_NAME": kprobe err %d\n", ret);

    if (kprobe_hit && (!f || (touch_name[0] && kprobe_hit->name &&
        strstr(kprobe_hit->name, touch_name))))
        f = kprobe_hit;

    if (ret == 0) unregister_kprobe(&kr);

    if (!f) return -ENODEV;
    G.touch = f;
    get_device(&G.touch->dev);
    G.found = true;

    pr_info(DRV_NAME": selected '%s' %04x:%04x\n",
            f->name ?: "?", f->id.vendor, f->id.product);
    if (f->absinfo)
        pr_info(DRV_NAME": X[%d,%d] Y[%d,%d]\n",
            f->absinfo[ABS_MT_POSITION_X].minimum,
            f->absinfo[ABS_MT_POSITION_X].maximum,
            f->absinfo[ABS_MT_POSITION_Y].minimum,
            f->absinfo[ABS_MT_POSITION_Y].maximum);
    return 0;
}

/* ---- 事件注入 ---- */
static inline void iev(__u16 t, __u16 c, __s32 v)
{
    if (likely(G.touch))
        input_event(G.touch, t, c, v);
}
static inline void iabs(__u16 c, __s32 v) { iev(EV_ABS, c, v); }
static inline void isyn(void) { iev(EV_SYN, SYN_REPORT, 0); }

static void ipt(const struct ti_point *p)
{
    if (p->x >= 0)          iabs(ABS_MT_POSITION_X, p->x);
    if (p->y >= 0)          iabs(ABS_MT_POSITION_Y, p->y);
    if (p->pressure > 0)    iabs(ABS_MT_PRESSURE, p->pressure);
    if (p->touch_major > 0) iabs(ABS_MT_TOUCH_MAJOR, p->touch_major);
    if (p->touch_minor > 0) iabs(ABS_MT_TOUCH_MINOR, p->touch_minor);
}

/* ---- ioctl 处理 ---- */
static int do_ti(unsigned int cmd, unsigned long arg)
{
    struct ti_cmd c;
    if (copy_from_user(&c, (void __user *)arg, sizeof(c)))
        return -EFAULT;

    switch (cmd) {
    case TI_DOWN:
        if (c.pt.x < 0 || c.pt.y < 0) return -EINVAL;
        iabs(ABS_MT_SLOT, c.slot);
        iabs(ABS_MT_TRACKING_ID, c.tracking_id);
        ipt(&c.pt); isyn();
        break;
    case TI_UP:
        iabs(ABS_MT_SLOT, c.slot);
        iabs(ABS_MT_TRACKING_ID, -1);
        isyn();
        break;
    case TI_MOVE:
        iabs(ABS_MT_SLOT, c.slot);
        ipt(&c.pt); isyn();
        break;
    case TI_SYNC:
        isyn();
        break;
    case TI_SLOT:
        iabs(ABS_MT_SLOT, c.slot);
        break;
    default:
        return -ENOTTY;
    }
    return 0;
}

/* ---- /dev/touch_inject ---- */
static long ti_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    if (_IOC_TYPE(cmd) != TI_IOC || _IOC_NR(cmd) > TI_MAX)
        return -ENOTTY;
    return do_ti(cmd, arg);
}

static const struct file_operations ti_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = ti_ioctl,
    .compat_ioctl   = ti_ioctl,
};

/* ---- /proc/touch_inject ---- */
static int proc_show(struct seq_file *m, void *v)
{
    seq_puts(m,   "touch_inject\n============\n");
    seq_printf(m, "device  : %s\n",
        G.found ? (G.touch->name ?: "?") : "NOT FOUND");
    if (G.found && G.touch->absinfo) {
        seq_printf(m, "x_range : %d - %d\n",
            G.touch->absinfo[ABS_MT_POSITION_X].minimum,
            G.touch->absinfo[ABS_MT_POSITION_X].maximum);
        seq_printf(m, "y_range : %d - %d\n",
            G.touch->absinfo[ABS_MT_POSITION_Y].minimum,
            G.touch->absinfo[ABS_MT_POSITION_Y].maximum);
        seq_printf(m, "vendor  : %04x\n", G.touch->id.vendor);
        seq_printf(m, "product : %04x\n", G.touch->id.product);
    }
    return 0;
}

static int proc_open(struct inode *i, struct file *f)
{
    return single_open(f, proc_show, NULL);
}

static const struct proc_ops proc_ops = {
    .proc_open    = proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static struct proc_dir_entry *proc_entry;

/* ---- init/exit ---- */
static int __init touch_inject_init(void)
{
    int ret;

    pr_info(DRV_NAME": init kernel %s\n", utsname()->release);
    memset(&G, 0, sizeof(G));

    ret = alloc_chrdev_region(&G.devno, 0, 1, DRV_NAME);
    if (ret) return ret;

    cdev_init(&G.cdev, &ti_fops);
    G.cdev.owner = THIS_MODULE;
    ret = cdev_add(&G.cdev, G.devno, 1);
    if (ret) goto e1;

    G.cls = class_create(THIS_MODULE, DRV_CLASS);
    if (IS_ERR(G.cls)) { ret = PTR_ERR(G.cls); goto e2; }

    G.dev = device_create(G.cls, NULL, G.devno, NULL, DRV_NAME);
    if (IS_ERR(G.dev)) { ret = PTR_ERR(G.dev); goto e3; }

    ret = find_ts();
    if (ret)
        pr_warn(DRV_NAME": touchscreen not found, /dev/%s ready anyway\n",
                DRV_NAME);

    proc_entry = proc_create(DRV_NAME, 0444, NULL, &proc_ops);
    pr_info(DRV_NAME": ready (/dev/%s)\n", DRV_NAME);
    return 0;

e3: class_destroy(G.cls);
e2: cdev_del(&G.cdev);
e1: unregister_chrdev_region(G.devno, 1);
    return ret;
}

static void __exit touch_inject_exit(void)
{
    if (G.touch) put_device(&G.touch->dev);
    if (proc_entry) proc_remove(proc_entry);
    device_destroy(G.cls, G.devno);
    class_destroy(G.cls);
    cdev_del(&G.cdev);
    unregister_chrdev_region(G.devno, 1);
    pr_info(DRV_NAME": unloaded\n");
}

module_init(touch_inject_init);
module_exit(touch_inject_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Claw");
MODULE_DESCRIPTION("Kernel touch injection via ioctl — no virtual input device");
MODULE_VERSION("1.0");
