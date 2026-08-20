#!/system/bin/sh

MODDIR=${0%/*}
sh "$MODDIR/provider-control.sh" "$MODDIR" stop >/dev/null 2>&1
rm -rf /data/adb/android_vcam_hidl_provider
