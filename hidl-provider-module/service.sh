#!/system/bin/sh

LOG_DIR=/data/adb/android_vcam_hidl_provider
BOOT_LOG=$LOG_DIR/bootstrap.log

mkdir -p "$LOG_DIR"
chmod 0700 "$LOG_DIR"

# Registration is owned exclusively by post-fs-data.sh because a declared HIDL
# service must exist before CameraService initializes. The one-shot script
# writes disable for the next boot, so KernelSU normally skips this stage.
echo "service stage reached; post-fs-data owns provider lifecycle" >> "$BOOT_LOG"
