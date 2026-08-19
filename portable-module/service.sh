#!/system/bin/sh

MODDIR=${0%/*}
LOG_DIR=/data/adb/android_vcam_portable
LOG_FILE=$LOG_DIR/bootstrap.log
mkdir -p "$LOG_DIR"
chmod 0700 "$LOG_DIR"

if [ -e "$MODDIR/disable" ]; then
    echo "service: module disabled" >> "$LOG_FILE"
    exit 0
fi

pid="$(pidof cameraserver 2>/dev/null | awk '{print $1}')"
{
    echo "service $(date '+%Y-%m-%dT%H:%M:%S%z')"
    echo "pid=$pid"
    [ -n "$pid" ] && echo "exe=$(readlink -f "/proc/$pid/exe" 2>/dev/null)"
    [ -n "$pid" ] && echo "context=$(cat "/proc/$pid/attr/current" 2>/dev/null)"
    echo "camera_services=$(service list 2>/dev/null | grep -c 'media.camera')"
} >> "$LOG_FILE"
