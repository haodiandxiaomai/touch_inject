# touch_inject - 内核级触摸注入模块
# 适配: 6.1.128-android14-2025-03-ReSukiSU

obj-m := touch_inject.o

# ===== 修改为你的内核源码路径 =====
# 如果用 GKI_KernelSU_SUSFS 构建:
#   KERNEL_SRC = /path/to/GKI_KernelSU_SUSFS/out/android14-6.1/dist
KERNEL_SRC ?= /path/to/kernel/source
ARCH       ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean

# 编译测试工具
test: test_inject
	$(CROSS_COMPILE)gcc -static -o test_inject test_inject.c

# 推送到设备
push: all test
	adb push touch_inject.ko /data/local/tmp/
	adb push test_inject /data/local/tmp/
	adb shell chmod 755 /data/local/tmp/test_inject

load:
	adb shell su -c "insmod /data/local/tmp/touch_inject.ko touch_name=$(TOUCH_NAME)"

unload:
	adb shell su -c "rmmod touch_inject"

status:
	adb shell cat /proc/touch_inject

test-run:
	adb shell /data/local/tmp/test_inject
