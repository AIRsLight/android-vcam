#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR=/data/adb/android_vcam_aidl_provider

echo "AIDL provider one-shot status"
echo "next_boot=disabled"
if sh "$MODDIR/provider-control.sh" "$MODDIR" status; then
    echo "The action button is intentionally read-only during zero-camera qualification"
else
    echo "Provider is not running; inspect $STATE_DIR before rebooting"
    exit 1
fi
