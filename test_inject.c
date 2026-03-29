/*
 * test_inject.c - 触摸注入测试程序 (userspace)
 *
 * 编译: aarch64-linux-gnu-gcc -static -o test_inject test_inject.c
 * 运行: ./test_inject
 *
 * 演示:
 *   1. 通过 /dev/touch_inject 注入触摸
 *   2. 通过 hook 后的 /dev/input/eventX 注入触摸
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <errno.h>

/* 和内核模块一致的定义 */
#define TOUCH_IOC_MAGIC  'T'

struct touch_point {
    int x;
    int y;
    int pressure;
    int touch_major;
    int touch_minor;
};

struct touch_inject_cmd {
    unsigned int cmd;
    struct touch_point point;
    unsigned int slot;
    unsigned int tracking_id;
};

#define TOUCH_IOCTL_MOVE      _IOW(TOUCH_IOC_MAGIC, 0x01, struct touch_inject_cmd)
#define TOUCH_IOCTL_DOWN      _IOW(TOUCH_IOC_MAGIC, 0x02, struct touch_inject_cmd)
#define TOUCH_IOCTL_UP        _IOW(TOUCH_IOC_MAGIC, 0x03, struct touch_inject_cmd)
#define TOUCH_IOCTL_SYNC      _IOW(TOUCH_IOC_MAGIC, 0x04, struct touch_inject_cmd)
#define TOUCH_IOCTL_MT_ID     _IOW(TOUCH_IOC_MAGIC, 0x05, struct touch_inject_cmd)
#define TOUCH_IOCTL_SET_SLOT  _IOW(TOUCH_IOC_MAGIC, 0x06, struct touch_inject_cmd)

/* 设备路径 */
#define INJECT_DEV  "/dev/touch_inject"

/* 模拟一次简单的点击操作 */
static int simulate_tap(int fd, int x, int y)
{
    struct touch_inject_cmd cmd;
    int ret;

    memset(&cmd, 0, sizeof(cmd));

    /* 1. 按下 */
    cmd.slot = 0;
    cmd.tracking_id = 0;
    cmd.point.x = x;
    cmd.point.y = y;
    cmd.point.pressure = 100;
    cmd.point.touch_major = 10;
    cmd.point.touch_minor = 10;

    ret = ioctl(fd, TOUCH_IOCTL_DOWN, &cmd);
    if (ret < 0) {
        perror("TOUCH_IOCTL_DOWN");
        return -1;
    }
    printf("[+] DOWN at (%d, %d)\n", x, y);

    usleep(50000); /* 50ms */

    /* 2. 抬起 */
    memset(&cmd, 0, sizeof(cmd));
    cmd.slot = 0;

    ret = ioctl(fd, TOUCH_IOCTL_UP, &cmd);
    if (ret < 0) {
        perror("TOUCH_IOCTL_UP");
        return -1;
    }
    printf("[+] UP\n");

    return 0;
}

/* 模拟滑动操作 */
static int simulate_swipe(int fd, int x1, int y1, int x2, int y2, int steps)
{
    struct touch_inject_cmd cmd;
    int ret, i;
    float dx, dy;

    memset(&cmd, 0, sizeof(cmd));

    /* 按下 */
    cmd.slot = 0;
    cmd.tracking_id = 100;
    cmd.point.x = x1;
    cmd.point.y = y1;
    cmd.point.pressure = 120;

    ret = ioctl(fd, TOUCH_IOCTL_DOWN, &cmd);
    if (ret < 0) {
        perror("TOUCH_IOCTL_DOWN");
        return -1;
    }
    printf("[+] SWIPE start (%d, %d)\n", x1, y1);

    dx = (float)(x2 - x1) / steps;
    dy = (float)(y2 - y1) / steps;

    for (i = 1; i <= steps; i++) {
        usleep(16000); /* ~60fps */

        memset(&cmd, 0, sizeof(cmd));
        cmd.slot = 0;
        cmd.point.x = x1 + (int)(dx * i);
        cmd.point.y = y1 + (int)(dy * i);
        cmd.point.pressure = 120;

        ret = ioctl(fd, TOUCH_IOCTL_MOVE, &cmd);
        if (ret < 0) {
            perror("TOUCH_IOCTL_MOVE");
            return -1;
        }
    }

    printf("[+] SWIPE end (%d, %d)\n", x2, y2);

    usleep(16000);

    /* 抬起 */
    memset(&cmd, 0, sizeof(cmd));
    cmd.slot = 0;

    ret = ioctl(fd, TOUCH_IOCTL_UP, &cmd);
    if (ret < 0) {
        perror("TOUCH_IOCTL_UP");
        return -1;
    }
    printf("[+] UP\n");

    return 0;
}

/* 模拟双指操作 */
static int simulate_two_finger(int fd)
{
    struct touch_inject_cmd cmd;
    int ret;

    printf("[+] Two-finger tap test\n");

    /* 手指1 按下 */
    memset(&cmd, 0, sizeof(cmd));
    cmd.slot = 0;
    cmd.tracking_id = 0;
    cmd.point.x = 400;
    cmd.point.y = 600;
    cmd.point.pressure = 100;
    ret = ioctl(fd, TOUCH_IOCTL_DOWN, &cmd);
    if (ret < 0) { perror("DOWN finger1"); return -1; }
    printf("  finger1 DOWN (400, 600)\n");

    usleep(20000);

    /* 手指2 按下 */
    memset(&cmd, 0, sizeof(cmd));
    cmd.slot = 1;
    cmd.tracking_id = 1;
    cmd.point.x = 700;
    cmd.point.y = 600;
    cmd.point.pressure = 100;
    ret = ioctl(fd, TOUCH_IOCTL_DOWN, &cmd);
    if (ret < 0) { perror("DOWN finger2"); return -1; }
    printf("  finger2 DOWN (700, 600)\n");

    usleep(100000);

    /* 手指2 抬起 */
    memset(&cmd, 0, sizeof(cmd));
    cmd.slot = 1;
    ret = ioctl(fd, TOUCH_IOCTL_UP, &cmd);
    if (ret < 0) { perror("UP finger2"); return -1; }
    printf("  finger2 UP\n");

    usleep(20000);

    /* 手指1 抬起 */
    memset(&cmd, 0, sizeof(cmd));
    cmd.slot = 0;
    ret = ioctl(fd, TOUCH_IOCTL_UP, &cmd);
    if (ret < 0) { perror("UP finger1"); return -1; }
    printf("  finger1 UP\n");

    return 0;
}

static void usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -d <device>   Device path (default: %s)\n", INJECT_DEV);
    printf("  -t <x,y>      Tap at (x,y)\n");
    printf("  -s <x1,y1,x2,y2>  Swipe from (x1,y1) to (x2,y2)\n");
    printf("  -2            Two-finger tap test\n");
    printf("  -D            Use hook test mode (requires input device path)\n");
    printf("\nExamples:\n");
    printf("  %s -t 540,960         # 点击屏幕中心\n", prog);
    printf("  %s -s 540,1800,540,200 # 上滑\n", prog);
    printf("  %s -2                  # 双指点击\n", prog);
}

int main(int argc, char *argv[])
{
    int fd = -1;
    int opt;
    const char *dev_path = INJECT_DEV;
    int do_tap = 0, do_swipe = 0, do_two = 0, do_hook = 0;
    int tap_x = 0, tap_y = 0;
    int sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0;
    const char *hook_dev = NULL;

    while ((opt = getopt(argc, argv, "d:t:s:2D:h")) != -1) {
        switch (opt) {
        case 'd':
            dev_path = optarg;
            break;
        case 't':
            do_tap = 1;
            sscanf(optarg, "%d,%d", &tap_x, &tap_y);
            break;
        case 's':
            do_swipe = 1;
            sscanf(optarg, "%d,%d,%d,%d", &sx1, &sy1, &sx2, &sy2);
            break;
        case '2':
            do_two = 1;
            break;
        case 'D':
            do_hook = 1;
            hook_dev = optarg;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    /* 打开设备 */
    if (do_hook) {
        fd = open(hook_dev, O_RDWR);
        if (fd < 0) {
            fprintf(stderr, "Cannot open %s: %s\n", hook_dev, strerror(errno));
            return 1;
        }
        printf("[*] Using hook device: %s\n", hook_dev);
    } else {
        fd = open(dev_path, O_RDWR);
        if (fd < 0) {
            fprintf(stderr, "Cannot open %s: %s\n", dev_path, strerror(errno));
            fprintf(stderr, "Hint: insmod touch_inject.ko && chmod 666 %s\n",
                    dev_path);
            return 1;
        }
        printf("[*] Using inject device: %s\n", dev_path);
    }

    /* 执行操作 */
    if (do_tap) {
        printf("[*] Tapping at (%d, %d)\n", tap_x, tap_y);
        simulate_tap(fd, tap_x, tap_y);
    }

    if (do_swipe) {
        printf("[*] Swiping (%d,%d) -> (%d,%d)\n", sx1, sy1, sx2, sy2);
        simulate_swipe(fd, sx1, sy1, sx2, sy2, 30);
    }

    if (do_two) {
        simulate_two_finger(fd);
    }

    if (!do_tap && !do_swipe && !do_two) {
        /* 默认: 演示点击 + 滑动 */
        printf("[*] Demo mode: tap + swipe\n\n");

        printf("--- Tap test ---\n");
        simulate_tap(fd, 540, 960);

        usleep(500000);

        printf("\n--- Swipe test (up) ---\n");
        simulate_swipe(fd, 540, 1800, 540, 200, 30);

        usleep(500000);

        printf("\n--- Swipe test (right) ---\n");
        simulate_swipe(fd, 100, 960, 1000, 960, 25);

        usleep(500000);

        printf("\n--- Two-finger test ---\n");
        simulate_two_finger(fd);

        printf("\n[+] Demo complete\n");
    }

    close(fd);
    return 0;
}
