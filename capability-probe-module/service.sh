#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR=/data/adb/android_vcam_capability_probe

mkdir -p "$STATE_DIR" || exit 1
chmod 0700 "$STATE_DIR"

# Wait only for the normal Android boot and stock CameraService. Timeout does
# not trigger recovery or service control; it merely records a blocked report.
attempt=0
while [ "$(getprop sys.boot_completed)" != "1" ] && [ "$attempt" -lt 90 ]; do
    sleep 2
    attempt=$((attempt + 1))
done

attempt=0
while ! service check media.camera 2>/dev/null | grep -q ': found$'; do
    [ "$attempt" -ge 30 ] && break
    sleep 1
    attempt=$((attempt + 1))
done

sh "$MODDIR/run-probe.sh" >"$STATE_DIR/service.log" 2>&1
chmod 0600 "$STATE_DIR/service.log" 2>/dev/null
