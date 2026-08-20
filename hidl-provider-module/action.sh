#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR=/data/adb/android_vcam_hidl_provider
MODE_FILE=$STATE_DIR/provider.mode

if sh "$MODDIR/provider-control.sh" "$MODDIR" status >/dev/null 2>&1 && \
   [ "$(cat "$MODE_FILE" 2>/dev/null)" = "two" ]; then
    echo "Switching HIDL provider to safe zero-camera mode"
    sh "$MODDIR/provider-control.sh" "$MODDIR" start-zero
else
    echo "Switching HIDL provider to two-camera test mode (IDs 1000/1001)"
    sh "$MODDIR/provider-control.sh" "$MODDIR" start-two
fi
