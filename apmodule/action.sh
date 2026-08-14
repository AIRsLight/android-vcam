#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR="/data/adb/android_vcam"
HAL_PATH="/vendor/lib64/hw/camera.qcom.so"

echo "android-vcam status"
if [ -e "$MODDIR/disable" ]; then
    echo "module: disabled for next boot"
else
    echo "module: enabled"
fi

if [ -e "$STATE_DIR/mount.ok" ]; then
    echo "mount: active"
else
    echo "mount: not confirmed"
fi

if [ -r "$HAL_PATH" ]; then
    echo "hal: $(sha256sum "$HAL_PATH" | awk '{print $1}')"
fi

dumpsys media.camera 2>/dev/null | grep -m 1 "Number of camera devices" || \
    echo "camera service: unavailable"
