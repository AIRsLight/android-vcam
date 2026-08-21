#!/system/bin/sh

MODDIR=${0%/*}
if [ "$(cat "$MODDIR/profile.id" 2>/dev/null)" = nx769j-ukq1-20240417 ]; then
    STATE_DIR=/data/adb/android_vcam/runtime/aidl
else
    STATE_DIR=/data/adb/android_vcam_aidl_provider
fi

echo "AIDL provider one-shot status"
echo "next_boot=disabled"
if sh "$MODDIR/provider-control.sh" "$MODDIR" status; then
    echo "The action button is intentionally read-only during zero-camera qualification"
else
    echo "Provider is not running; inspect $STATE_DIR before rebooting"
    exit 1
fi
