#!/system/bin/sh

TARGET_DEVICE="OnePlus7Pro"
TARGET_PRODUCT_NAME="OnePlus7Pro_CH"
TARGET_API="31"
TARGET_ABI="arm64-v8a"
TARGET_FINGERPRINT="OnePlus/OnePlus7Pro_CH/OnePlus7Pro:12/SKQ1.211113.001/P.202303230244:user/release-keys"
TARGET_HAL_HASH="dab50dfd0bde9f710c92097442d6451695f7ef82cb9e836b0af2b9369751daa6"
LEGACY_HAL_HASH="66d5f38e8a6f5a287a661a06e1224fef477bb41574ca61f7091b5682b9b587d5"
TARGET_CAMERASERVICE_HASH="2108be5d63b385282d844f689e9f34740026072b8ef6daca2ed59b23612870af"
HAL_PATH="/vendor/lib64/hw/camera.qcom.so"
PROXY_SLOT_PATH="/vendor/lib64/hw/local_time.default.so"
PROXY_SLOT_HASH="6ac900f7c1b17fb5551a673ded1fc11469c53dac329bcbbb17b97dd57d2cc992"
CAMERASERVICE_PATH="/system/lib64/libcameraservice.so"
MODULE_HAL="$MODPATH/vendor/lib64/hw/camera.qcom.so"
[ -f "$MODULE_HAL" ] || MODULE_HAL="$MODPATH/system/vendor/lib64/hw/camera.qcom.so"
PROXY_LIBRARY="$MODPATH/vendor/lib64/libvcam_proxy.so"
[ -f "$PROXY_LIBRARY" ] || PROXY_LIBRARY="$MODPATH/system/vendor/lib64/libvcam_proxy.so"
MODULE_CAMERASERVICE="$MODPATH/system/lib64/libcameraservice.so"
FRAME_PUBLISHER="$MODPATH/vendor/bin/vcam-publisher"
[ -f "$FRAME_PUBLISHER" ] || FRAME_PUBLISHER="$MODPATH/system/vendor/bin/vcam-publisher"
STREAM_PROVIDER="$MODPATH/system/bin/vcam-streamer"
CONTROL_DAEMON="$MODPATH/system/bin/vcamd"
HTTPS_DOWNLOADER="$MODPATH/system/framework/vcam-https-downloader.jar"
INSTALLED_MODULE_DIR="/data/adb/modules/android_vcam"

require_equal() {
    label="$1"
    actual="$2"
    expected="$3"
    if [ "$actual" != "$expected" ]; then
        abort "! $label mismatch: expected '$expected', got '$actual'"
    fi
}

matches_installed_payload() {
    actual_hash="$1"
    shift
    for relative_path in "$@"; do
        installed_path="$INSTALLED_MODULE_DIR/$relative_path"
        [ -f "$installed_path" ] || continue
        installed_hash="$(sha256sum "$installed_path" | awk '{print $1}')"
        [ "$actual_hash" = "$installed_hash" ] && return 0
    done
    return 1
}

ui_print "- Checking target device"
require_equal "device" "$(getprop ro.product.device)" "$TARGET_DEVICE"
require_equal "product name" "$(getprop ro.product.name)" "$TARGET_PRODUCT_NAME"
require_equal "API" "$(getprop ro.build.version.sdk)" "$TARGET_API"
require_equal "ABI" "$(getprop ro.product.cpu.abi)" "$TARGET_ABI"
require_equal "fingerprint" "$(getprop ro.build.fingerprint)" "$TARGET_FINGERPRINT"

[ -f "$HAL_PATH" ] || abort "! Original camera HAL is missing: $HAL_PATH"
[ -f "$PROXY_SLOT_PATH" ] || abort "! Proxy mount slot is missing"
[ -f "$CAMERASERVICE_PATH" ] || abort "! Camera service library is missing"
[ -f "$MODULE_HAL" ] || abort "! Packaged virtual camera HAL is missing"
[ -f "$PROXY_LIBRARY" ] || abort "! Packaged proxy library is missing"
[ -f "$MODULE_CAMERASERVICE" ] || abort "! Packaged camera service patch is missing"
[ -f "$FRAME_PUBLISHER" ] || abort "! Packaged frame publisher is missing"
[ -f "$STREAM_PROVIDER" ] || abort "! Packaged stream provider is missing"
[ -f "$CONTROL_DAEMON" ] || abort "! Packaged control daemon is missing"
[ -f "$HTTPS_DOWNLOADER" ] || abort "! Packaged HTTPS downloader is missing"

actual_hash="$(sha256sum "$HAL_PATH" | awk '{print $1}')"
if [ "$actual_hash" != "$TARGET_HAL_HASH" ] && [ "$actual_hash" != "$LEGACY_HAL_HASH" ]; then
    if matches_installed_payload "$actual_hash" \
            "vendor/lib64/hw/camera.qcom.so" \
            "system/vendor/lib64/hw/camera.qcom.so"; then
        ui_print "- Recognized mounted HAL from installed android_vcam module"
    else
        abort "! camera HAL hash mismatch: got '$actual_hash'"
    fi
fi
proxy_slot_hash="$(sha256sum "$PROXY_SLOT_PATH" | awk '{print $1}')"
if [ "$proxy_slot_hash" != "$PROXY_SLOT_HASH" ]; then
    if matches_installed_payload "$proxy_slot_hash" \
            "vendor/lib64/libvcam_proxy.so" \
            "system/vendor/lib64/libvcam_proxy.so"; then
        ui_print "- Recognized mounted proxy from installed android_vcam module"
    else
        abort "! proxy mount slot hash mismatch: got '$proxy_slot_hash'"
    fi
fi
camera_service_hash="$(sha256sum "$CAMERASERVICE_PATH" | awk '{print $1}')"
module_camera_service_hash="$(sha256sum "$MODULE_CAMERASERVICE" | awk '{print $1}')"
if [ "$camera_service_hash" != "$TARGET_CAMERASERVICE_HASH" ] && \
   [ "$camera_service_hash" != "$module_camera_service_hash" ]; then
    if matches_installed_payload "$camera_service_hash" \
            "system/lib64/libcameraservice.so"; then
        ui_print "- Recognized mounted CameraService from installed android_vcam module"
    else
        abort "! cameraservice hash mismatch: got '$camera_service_hash'"
    fi
fi

module_size="$(wc -c < "$MODULE_HAL")"
[ "$module_size" -gt 65536 ] || abort "! Packaged HAL is unexpectedly small"

ui_print "- Device checks passed"
ui_print "- This module uses guarded bind mounts; no metamodule is required"
ui_print "- Know the APatch volume-down Safe Mode procedure before rebooting"

set_perm "$MODULE_HAL" 0 0 0644 u:object_r:vendor_file:s0
set_perm "$PROXY_LIBRARY" 0 0 0644 u:object_r:vendor_file:s0
set_perm "$MODULE_CAMERASERVICE" 0 0 0644 u:object_r:system_lib_file:s0
set_perm "$FRAME_PUBLISHER" 0 0 0755 u:object_r:vendor_file:s0
set_perm "$STREAM_PROVIDER" 0 2000 0755 u:object_r:system_file:s0
set_perm "$CONTROL_DAEMON" 0 0 0755 u:object_r:system_file:s0
set_perm "$HTTPS_DOWNLOADER" 0 0 0644 u:object_r:system_file:s0
set_perm "$MODPATH/post-mount.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/boot-completed.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/vcamctl" 0 0 0755
set_perm "$MODPATH/provider-runner.sh" 0 0 0755
set_perm "$MODPATH/device-probe.sh" 0 0 0755
