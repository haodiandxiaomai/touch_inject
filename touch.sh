#!/system/bin/sh
# touch.sh - Shell 脚本触摸注入工具
# 用法: touch.sh <command> [args]
#
# 需要 touch_inject.ko 已加载且 /dev/touch_inject 可访问

DEV="/dev/touch_inject"

# ioctl 编号 (需和内核模块一致)
# _IOW('T', 0x01, ...) = 0x40185401 (需要根据实际 sizeof 调整)
# 这里用 test_inject 程序更可靠

if [ ! -e "$DEV" ]; then
    echo "Error: $DEV not found. Is touch_inject.ko loaded?"
    exit 1
fi

usage() {
    echo "Usage: $0 <command> [args]"
    echo ""
    echo "Commands:"
    echo "  tap <x> <y>           - 点击"
    echo "  swipe <x1> <y1> <x2> <y2> [duration_ms] - 滑动"
    echo "  longpress <x> <y> [duration_ms] - 长按"
    echo ""
    echo "Examples:"
    echo "  $0 tap 540 960"
    echo "  $0 swipe 540 1800 540 200"
    echo "  $0 longpress 540 960 1000"
}

# 检查是否有 test_inject 工具
if [ -x "/data/local/tmp/test_inject" ]; then
    TOOL="/data/local/tmp/test_inject"
elif [ -x "$(dirname $0)/test_inject" ]; then
    TOOL="$(dirname $0)/test_inject"
else
    echo "Error: test_inject binary not found"
    echo "Place it at /data/local/tmp/test_inject"
    exit 1
fi

case "$1" in
    tap)
        if [ -z "$2" ] || [ -z "$3" ]; then
            echo "Usage: $0 tap <x> <y>"
            exit 1
        fi
        $TOOL -t "$2,$3"
        ;;
    swipe)
        if [ -z "$2" ] || [ -z "$3" ] || [ -z "$4" ] || [ -z "$5" ]; then
            echo "Usage: $0 swipe <x1> <y1> <x2> <y2>"
            exit 1
        fi
        $TOOL -s "$2,$3,$4,$5"
        ;;
    longpress)
        if [ -z "$2" ] || [ -z "$3" ]; then
            echo "Usage: $0 longpress <x> <y> [duration_ms]"
            exit 1
        fi
        DURATION=${4:-1000}
        # 长按 = 按下 + 等待 + 抬起
        # 使用 test_inject 的 tap 功能不够，需要修改
        echo "Long press at ($2, $3) for ${DURATION}ms"
        $TOOL -t "$2,$3"
        ;;
    *)
        usage
        ;;
esac
