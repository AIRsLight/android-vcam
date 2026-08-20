#!/system/bin/sh

ui_print "- Validating Android 14 HIDL provider probe"

sdk="$(getprop ro.build.version.sdk)"
abi="$(getprop ro.product.cpu.abi)"
fingerprint="$(getprop ro.build.fingerprint)"
expected_fingerprint='nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys'
vendor_sku="$(getprop ro.boot.product.vendor.sku)"
vendor_manifest=/vendor/etc/vintf/manifest.xml

if [ -n "$vendor_sku" ] && \
   [ -f "/vendor/etc/vintf/manifest_${vendor_sku}.xml" ]; then
    vendor_manifest="/vendor/etc/vintf/manifest_${vendor_sku}.xml"
fi
target_fcm="$(sed -n 's/.*target-level="\([0-9][0-9]*\)".*/\1/p' \
    "$vendor_manifest" 2>/dev/null | head -n 1)"

[ "$sdk" = "34" ] || abort "! This probe is restricted to Android 14"
[ "$abi" = "arm64-v8a" ] || abort "! This probe requires arm64-v8a"
[ "$fingerprint" = "$expected_fingerprint" ] || \
    abort "! This development probe is restricted to the qualified NX769J build"
[ -n "$target_fcm" ] || \
    abort "! Unable to determine the device target FCM from $vendor_manifest"
[ "$target_fcm" -lt 8 ] || \
    abort "! HIDL camera providers are incompatible with target FCM $target_fcm; use the AIDL v2 probe"
[ -d /data/adb/modules/meta-overlayfs ] || \
    abort "! The OverlayFS MetaModule must be installed first"

binary="$MODPATH/payload/bin/vcam_hidl_provider"
module="$MODPATH/payload/lib64/hw/camera.vcam.so"
libdir="$MODPATH/payload/lib64"
fragment="$MODPATH/system/vendor/etc/vintf/manifest/android.hardware.camera.provider@2.4-vcam-service.xml"
vendor_etc="$MODPATH/system/vendor/etc"
vintf_dir="$vendor_etc/vintf"
manifest_dir="$vintf_dir/manifest"

for required in \
    "$binary" \
    "$module" \
    "$fragment" \
    "$MODPATH/provider-control.sh" \
    "$MODPATH/post-fs-data.sh" \
    "$MODPATH/service.sh" \
    "$MODPATH/action.sh" \
    "$MODPATH/uninstall.sh"; do
    [ -f "$required" ] || abort "! Required probe file is missing: $required"
done

set_perm "$binary" 0 0 0755
set_perm_recursive "$libdir" 0 0 0755 0644
set_perm "$fragment" 0 0 0644
for config_dir in "$vendor_etc" "$vintf_dir" "$manifest_dir"; do
    chcon u:object_r:vendor_configs_file:s0 "$config_dir" || \
        abort "! Unable to label VINTF directory as vendor configuration: $config_dir"
done
chcon u:object_r:vendor_configs_file:s0 "$fragment" || \
    abort "! Unable to label the VINTF fragment as vendor configuration"
set_perm "$MODPATH/provider-control.sh" 0 0 0755
set_perm "$MODPATH/post-fs-data.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755

ui_print "- Adds only one VINTF manifest fragment through meta-overlayfs"
ui_print "- The provider registers in post-fs-data before CameraService starts"
ui_print "- The qualification advertises zero cameras and disables its next boot"
ui_print "- A 180-second boot watchdog requests a disabled recovery reboot"
ui_print "- Reboot is required; KernelSU bootloop protection remains the recovery path"
