#!/system/bin/sh

STATE_DIR=/data/adb/android_vcam_aidl_provider
BOOT_LOG=$STATE_DIR/bootstrap.log

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"
echo "service stage reached; post-fs-data owns AIDL provider lifecycle" >> "$BOOT_LOG"
