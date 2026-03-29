#!/system/bin/sh
# post-fs-data.sh - 在文件系统挂载后执行

MODDIR=${0%/*}

# 加载内核模块
insmod $MODDIR/touch_inject.ko

# 等待设备节点创建
sleep 1

# 设置权限 (需要在 service.sh 中再设置一次，因为 post-fs-data 阶段可能权限还没完全就绪)
if [ -e /dev/touch_inject ]; then
    chmod 666 /dev/touch_inject
fi
