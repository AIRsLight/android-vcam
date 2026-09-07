#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR=/data/adb/android_vcam
LOG_FILE="$STATE_DIR/module.log"
MOUNT_OK="$STATE_DIR/mount.ok"
PROFILE_FILE="$MODDIR/oneplus-profile.conf"
TARGET_HAL=/vendor/lib64/hw/camera.qcom.so
TARGET_SNAPSHOT=/vendor/lib64/hw/local_time.default.so
MODULE_HAL="$MODDIR/system/vendor/lib64/hw/camera.qcom.so"
MODULE_SNAPSHOT="$MODDIR/system/vendor/lib64/hw/local_time.default.so"

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"
rm -f "$MOUNT_OK"
[ ! -e "$MODDIR/disable" ] || exit 0

fail_closed() {
    echo "oneplus-global: $1" >> "$LOG_FILE"
    # This flag prevents the next boot's activation; MetaModule has already
    # mounted, so it does not undo overlays in the current boot.
    touch "$MODDIR/disable"
    exit 1
}

for required in "$PROFILE_FILE" "$TARGET_HAL" "$TARGET_SNAPSHOT" \
                "$MODULE_HAL" "$MODULE_SNAPSHOT"; do
    [ -f "$required" ] || fail_closed "required file missing: $required"
done

expected_fingerprint="$(sed -n 's/^fingerprint=//p' "$PROFILE_FILE" | head -n 1)"
[ -n "$expected_fingerprint" ] && \
    [ "$(getprop ro.build.fingerprint)" = "$expected_fingerprint" ] || \
    fail_closed "firmware changed; reboot to stock and reinstall the adapter"

expected_original="$(sed -n 's/^original_camera_hal_sha256=//p' "$PROFILE_FILE" | head -n 1)"
expected_shim="$(sed -n 's/^shim_sha256=//p' "$PROFILE_FILE" | head -n 1)"
[ -n "$expected_original" ] && [ -n "$expected_shim" ] || \
    fail_closed "profile hashes are missing"
[ "$(sha256sum "$MODULE_HAL" | awk '{print $1}')" = "$expected_shim" ] || \
    fail_closed "packaged shim hash changed"
[ "$(sha256sum "$MODULE_SNAPSHOT" | awk '{print $1}')" = "$expected_original" ] || \
    fail_closed "OEM snapshot hash changed"
[ "$(sha256sum "$TARGET_HAL" | awk '{print $1}')" = "$expected_shim" ] || \
    fail_closed "MetaModule did not mount the camera shim"
[ "$(sha256sum "$TARGET_SNAPSHOT" | awk '{print $1}')" = "$expected_original" ] || \
    fail_closed "MetaModule did not mount the OEM HAL snapshot"

touch "$MOUNT_OK"
chmod 0600 "$MOUNT_OK"
echo "oneplus-global: verified shim=$expected_shim original=$expected_original" >> "$LOG_FILE"
