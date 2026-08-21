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

attempt=0
stable=0
stable_pid=""
pid=""
while [ "$attempt" -lt 450 ]; do
    current_pid="$(pidof cameraserver 2>/dev/null | awk '{print $1}')"
    if [ -n "$current_pid" ] && \
       service check media.camera 2>/dev/null | grep -q ': found$'; then
        if [ "$current_pid" = "$stable_pid" ]; then
            stable=$((stable + 1))
        else
            stable_pid="$current_pid"
            stable=1
        fi
        if [ "$stable" -ge 100 ]; then
            pid="$current_pid"
            break
        fi
    else
        stable=0
        stable_pid=""
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done

if [ -z "$pid" ]; then
    echo "service: CameraService did not remain stable for 10 seconds; recovering stock" >> "$LOG_FILE"
    touch "$MODDIR/disable"
    if [ -x /system/bin/vcam/cameraserver ] && [ -e /system/bin/cameraserver ]; then
        if mount -o bind /system/bin/vcam/cameraserver /system/bin/cameraserver 2>> "$LOG_FILE"; then
            echo "service: stock cameraserver rebound for recovery reboot" >> "$LOG_FILE"
        else
            echo "service: emergency stock bind failed" >> "$LOG_FILE"
        fi
    fi
    # NX769J's OEM provider deliberately aborts when cameraserver dies. Never
    # restart cameraserver in isolation: persist the disabled marker and let a
    # complete reboot tear down the camera stack in normal init order.
    sync
    echo "service: module disabled; requesting full recovery reboot" >> "$LOG_FILE"
    setprop sys.powerctl reboot,vcam-bootstrap-recovery
else
    echo "service: CameraService stable for 10 seconds" >> "$LOG_FILE"
    touch "$MODDIR/disable"
    sync
    echo "service: armed automatic disable for the next boot" >> "$LOG_FILE"
fi

{
    echo "service $(date '+%Y-%m-%dT%H:%M:%S%z')"
    echo "pid=$pid"
    [ -n "$pid" ] && echo "exe=$(readlink -f "/proc/$pid/exe" 2>/dev/null)"
    [ -n "$pid" ] && echo "context=$(cat "/proc/$pid/attr/current" 2>/dev/null)"
    echo "camera_services=$(service list 2>/dev/null | grep -c 'media.camera')"
} >> "$LOG_FILE"
