#!/system/bin/sh

ui_print "- Validating Android 14 HIDL provider probe"

sdk="$(getprop ro.build.version.sdk)"
abi="$(getprop ro.product.cpu.abi)"
fingerprint="$(getprop ro.build.fingerprint)"
expected_fingerprint='nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys'

[ "$sdk" = "34" ] || abort "! This probe is restricted to Android 14"
[ "$abi" = "arm64-v8a" ] || abort "! This probe requires arm64-v8a"
[ "$fingerprint" = "$expected_fingerprint" ] || \
    abort "! This development probe is restricted to the qualified NX769J build"
[ -d /data/adb/modules/meta-overlayfs ] || \
    abort "! The OverlayFS MetaModule must be installed first"

binary="$MODPATH/payload/bin/vcam_hidl_provider"
module="$MODPATH/payload/lib64/hw/camera.vcam.so"
libdir="$MODPATH/payload/lib64"
fragment="$MODPATH/system/vendor/etc/vintf/manifest/android.hardware.camera.provider@2.4-vcam-service.xml"

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
chcon u:object_r:vendor_configs_file:s0 "$fragment" || \
    abort "! Unable to label the VINTF fragment as vendor configuration"
set_perm "$MODPATH/provider-control.sh" 0 0 0755
set_perm "$MODPATH/post-fs-data.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755

ui_print "- Adds only one VINTF manifest fragment through meta-overlayfs"
ui_print "- The provider starts after boot and advertises zero cameras by default"
ui_print "- The action button toggles safe zero-camera and two-camera test modes"
ui_print "- A 180-second boot watchdog disables the module before recovery reboot"
ui_print "- Reboot is required; KernelSU bootloop protection remains the recovery path"
