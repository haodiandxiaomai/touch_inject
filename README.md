# touch_inject — 内核级触摸注入

**内核:** 6.1.128-android14-2025-03-ReSukiSU
**原理:** 不注册虚拟 input 设备，直接调用 `input_event()` 向 input 子系统注入事件。事件经过 evdev 分发，和真实硬件触摸完全一致。

## 文件

```
touch_inject.c    — 内核模块 (cdev + ioctl hook)
test_inject.c     — userspace 测试程序
Makefile          — 交叉编译
magisk_module/    — Magisk 模块模板
```

## 编译

```bash
# 设置你的内核源码路径
export KERNEL_SRC=/path/to/GKI_KernelSU_SUSFS/out/android14-6.1/dist

# 编译模块
make KERNEL_SRC=$KERNEL_SRC CROSS_COMPILE=aarch64-linux-gnu-

# 编译测试程序
aarch64-linux-gnu-gcc -static -o test_inject test_inject.c
```

## 使用

```bash
# 加载
adb push touch_inject.ko /data/local/tmp/
adb shell su -c "insmod /data/local/tmp/touch_inject.ko"
adb shell su -c "chmod 666 /dev/touch_inject"

# 查看状态
adb shell cat /proc/touch_inject

# 如果自动查找失败，手动指定触摸屏名字
adb shell su -c "rmmod touch_inject"
adb shell su -c "insmod /data/local/tmp/touch_inject.ko touch_name=你的触摸屏名字"

# 测试
adb push test_inject /data/local/tmp/
adb shell chmod 755 /data/local/tmp/test_inject
adb shell /data/local/tmp/test_inject
```

## ioctl 接口

```c
#define TI_IOC_MAGIC  'T'

struct ti_point { int x, y, pressure, touch_major, touch_minor; };
struct ti_cmd   { unsigned int cmd; struct ti_point pt; unsigned int slot, tracking_id; };

TI_DOWN      _IOW('T', 0x01, struct ti_cmd)  // 按下
TI_UP        _IOW('T', 0x02, struct ti_cmd)  // 抬起
TI_MOVE      _IOW('T', 0x03, struct ti_cmd)  // 移动
TI_SYNC      _IOW('T', 0x04, struct ti_cmd)  // 同步
TI_SET_SLOT  _IOW('T', 0x05, struct ti_cmd)  // 切换 slot
```

**两种调用方式:**
1. `/dev/touch_inject` — 字符设备 (推荐)
2. `/dev/input/eventX` — hook 后的触摸屏设备 (需要 ioctl hook 生效)

## 调用示例 (C)

```c
int fd = open("/dev/touch_inject", O_RDWR);

struct ti_cmd c = {
    .slot = 0,
    .tracking_id = 0,
    .pt = { .x = 540, .y = 960, .pressure = 100 }
};
ioctl(fd, TI_DOWN, &c);  // 按下

c.pt.x = 540; c.pt.y = 200;
ioctl(fd, TI_MOVE, &c);  // 滑动

c.slot = 0;
ioctl(fd, TI_UP, &c);    // 抬起
```

## 注意事项

- 需要 root 权限
- 坐标范围取决于你的屏幕分辨率，先看 `/proc/touch_inject` 确认
- 多点触控通过 slot 区分不同触点
- ioctl hook 修改了 input_dev->dev.fops 指针，如果内核开启了 `CONFIG_STRICT_MEMORY_RWX`，可能需要额外的 `set_memory_rw()` 调用
