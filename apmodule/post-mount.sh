#!/system/bin/sh

MODDIR=${0%/*}
LOG_DIR="/data/adb/android_vcam"
LOG_FILE="$LOG_DIR/module.log"
MOUNT_OK="$LOG_DIR/mount.ok"
TARGET_HAL="/vendor/lib64/hw/camera.qcom.so"
TARGET_PROXY_SLOT="/vendor/lib64/hw/local_time.default.so"
TARGET_CAMERASERVICE="/system/lib64/libcameraservice.so"
ORIGINAL_HAL_HASH="dab50dfd0bde9f710c92097442d6451695f7ef82cb9e836b0af2b9369751daa6"
LEGACY_HAL_HASH="66d5f38e8a6f5a287a661a06e1224fef477bb41574ca61f7091b5682b9b587d5"
ORIGINAL_PROXY_SLOT_HASH="6ac900f7c1b17fb5551a673ded1fc11469c53dac329bcbbb17b97dd57d2cc992"
ORIGINAL_CAMERASERVICE_HASH="2108be5d63b385282d844f689e9f34740026072b8ef6daca2ed59b23612870af"

mkdir -p "$LOG_DIR"
chmod 0700 "$LOG_DIR"
rm -f "$MOUNT_OK"

fail_mount() {
    echo "post-mount: $1" >> "$LOG_FILE"
    [ "$mounted_hal" = true ] && umount "$TARGET_HAL" 2>/dev/null
    [ "$mounted_camera_service" = true ] && umount "$TARGET_CAMERASERVICE" 2>/dev/null
    [ "$mounted_proxy" = true ] && umount "$TARGET_PROXY_SLOT" 2>/dev/null
    touch "$MODDIR/disable"
    exit 1
}

[ ! -e "$MODDIR/disable" ] || exit 0

MODULE_HAL="$MODDIR/vendor/lib64/hw/camera.qcom.so"
[ -f "$MODULE_HAL" ] || MODULE_HAL="$MODDIR/system/vendor/lib64/hw/camera.qcom.so"
MODULE_PROXY="$MODDIR/vendor/lib64/libvcam_proxy.so"
[ -f "$MODULE_PROXY" ] || MODULE_PROXY="$MODDIR/system/vendor/lib64/libvcam_proxy.so"
MODULE_CAMERASERVICE="$MODDIR/system/lib64/libcameraservice.so"
for required in "$MODULE_HAL" "$MODULE_PROXY" "$MODULE_CAMERASERVICE" \
                "$TARGET_HAL" "$TARGET_PROXY_SLOT" "$TARGET_CAMERASERVICE"; do
    [ -f "$required" ] || fail_mount "required file missing: $required"
done

hal_hash="$(sha256sum "$MODULE_HAL" | awk '{print $1}')"
proxy_hash="$(sha256sum "$MODULE_PROXY" | awk '{print $1}')"
camera_service_hash="$(sha256sum "$MODULE_CAMERASERVICE" | awk '{print $1}')"
target_hal_hash="$(sha256sum "$TARGET_HAL" | awk '{print $1}')"
target_proxy_hash="$(sha256sum "$TARGET_PROXY_SLOT" | awk '{print $1}')"
target_camera_service_hash="$(sha256sum "$TARGET_CAMERASERVICE" | awk '{print $1}')"

case "$target_proxy_hash" in
    "$proxy_hash") ;;
    "$ORIGINAL_PROXY_SLOT_HASH")
        mount -o bind "$MODULE_PROXY" "$TARGET_PROXY_SLOT" || \
            fail_mount "proxy bind failed"
        mounted_proxy=true
        ;;
    *) fail_mount "unexpected proxy slot hash $target_proxy_hash" ;;
esac

case "$target_camera_service_hash" in
    "$camera_service_hash") ;;
    "$ORIGINAL_CAMERASERVICE_HASH")
        mount -o bind "$MODULE_CAMERASERVICE" "$TARGET_CAMERASERVICE" || \
            fail_mount "cameraservice bind failed"
        mounted_camera_service=true
        ;;
    *) fail_mount "unexpected cameraservice hash $target_camera_service_hash" ;;
esac

case "$target_hal_hash" in
    "$hal_hash") ;;
    "$ORIGINAL_HAL_HASH"|"$LEGACY_HAL_HASH")
        mount -o bind "$MODULE_HAL" "$TARGET_HAL" || fail_mount "HAL bind failed"
        mounted_hal=true
        ;;
    *) fail_mount "unexpected target HAL hash $target_hal_hash" ;;
esac

[ "$(sha256sum "$TARGET_PROXY_SLOT" | awk '{print $1}')" = "$proxy_hash" ] || \
    fail_mount "mounted proxy hash mismatch"
[ "$(sha256sum "$TARGET_CAMERASERVICE" | awk '{print $1}')" = "$camera_service_hash" ] || \
    fail_mount "mounted cameraservice hash mismatch"
[ "$(sha256sum "$TARGET_HAL" | awk '{print $1}')" = "$hal_hash" ] || \
    fail_mount "mounted HAL hash mismatch"

touch "$MOUNT_OK"
{
    echo "post-mount $(date '+%Y-%m-%dT%H:%M:%S%z')"
    echo "module=$MODDIR"
    echo "hal=$hal_hash"
    echo "proxy_slot=$TARGET_PROXY_SLOT"
    echo "proxy=$proxy_hash"
    echo "cameraservice=$camera_service_hash"
} >> "$LOG_FILE"
