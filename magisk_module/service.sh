#!/system/bin/sh
# service.sh - 在系统启动完成后执行

MODDIR=${0%/*}

# 等待系统完全启动
until [ "$(getprop sys.boot_completed)" = "1" ]; do
    sleep 1
done

sleep 2

# 确保设备节点权限正确
if [ -e /dev/touch_inject ]; then
    chmod 666 /dev/touch_inject
    chown root:input /dev/touch_inject
fi

# 检查模块加载状态
if lsmod | grep -q touch_inject; then
    log -t touch_inject "Module loaded successfully"
    cat /proc/touch_inject
else
    log -t touch_inject "Module not loaded, attempting reload"
    insmod $MODDIR/touch_inject.ko
    sleep 1
    [ -e /dev/touch_inject ] && chmod 666 /dev/touch_inject
fi
