#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR=/data/adb/android_vcam_provider_probe
PID_FILE=$STATE_DIR/provider.pid
BINARY=$MODDIR/payload/bin/android.hardware.camera.provider-service-vcam-v2

if [ -s "$PID_FILE" ]; then
    pid="$(cat "$PID_FILE" 2>/dev/null)"
    case "$pid" in
        ''|*[!0-9]*) pid='' ;;
    esac
    if [ -n "$pid" ] && [ -d "/proc/$pid" ] && \
        [ "$(readlink "/proc/$pid/exe" 2>/dev/null)" = "$BINARY" ]; then
        kill "$pid" 2>/dev/null
    fi
fi

rm -rf /data/adb/android_vcam_provider_probe
