#!/system/bin/sh

MODDIR=${0%/*}
LOG_DIR=/data/adb/android_vcam_hidl_provider
BOOT_LOG=$LOG_DIR/bootstrap.log

mkdir -p "$LOG_DIR"
chmod 0700 "$LOG_DIR"

[ ! -e "$MODDIR/disable" ] || exit 0

attempt=0
while [ "$attempt" -lt 300 ]; do
    if [ "$(getprop sys.boot_completed)" = "1" ] && \
       service check media.camera 2>/dev/null | grep -q ': found$'; then
        break
    fi
    sleep 0.2
    attempt=$((attempt + 1))
done

if [ "$attempt" -ge 300 ]; then
    echo "CameraService was not ready after 60 seconds; provider not started" >> "$BOOT_LOG"
    exit 1
fi

if sh "$MODDIR/provider-control.sh" "$MODDIR" start-zero >> "$BOOT_LOG" 2>&1; then
    echo "safe zero-camera provider started" >> "$BOOT_LOG"
else
    echo "zero-camera provider failed; stock CameraService left running" >> "$BOOT_LOG"
    exit 1
fi
