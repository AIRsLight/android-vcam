#!/system/bin/sh

ui_print "- Validating Android 14 AIDL provider probe"

sdk="$(getprop ro.build.version.sdk)"
abi="$(getprop ro.product.cpu.abi)"
fingerprint="$(getprop ro.build.fingerprint)"
expected_fingerprint='nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys'
expected_cameraservice_hash='a26f8ee10002769428871e042c7993e87ad769703897dd75a2fb93a725c64438'
expected_camera_client_hash='1cf518e86a2e5461e585d8dbd7a1dbc93e7ba2bcc95c3e254ebdcc72ee0433c5'
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
cameraservice_hash="$(sha256sum /system/lib64/libcameraservice.so 2>/dev/null | awk '{print $1}')"
camera_client_hash="$(sha256sum /system/lib64/libcamera_client.so 2>/dev/null | awk '{print $1}')"
[ "$cameraservice_hash" = "$expected_cameraservice_hash" ] || \
    abort "! CameraService ABI mismatch: $cameraservice_hash"
[ "$camera_client_hash" = "$expected_camera_client_hash" ] || \
    abort "! libcamera_client ABI mismatch: $camera_client_hash"
[ "$target_fcm" = "8" ] || \
    abort "! This probe requires target FCM 8; found '${target_fcm:-unknown}'"
[ -d /data/adb/metamodule ] || \
    abort "! An active MetaModule must be installed first"
[ ! -e /data/adb/metamodule/disable ] || \
    abort "! The active MetaModule is disabled"
meta_flag="$(sed -n 's/^metamodule=//p' /data/adb/metamodule/module.prop 2>/dev/null | head -n 1)"
case "$meta_flag" in
    1|true) ;;
    *) abort "! /data/adb/metamodule is not a valid MetaModule" ;;
esac
if [ -d /data/adb/modules/android_vcam_hidl_provider ] && \
   [ ! -e /data/adb/modules/android_vcam_hidl_provider/disable ]; then
    abort "! Disable the HIDL provider probe before installing the AIDL probe"
fi

binary="$MODPATH/payload/bin/android.hardware.camera.provider-service-vcam-v2"
libdir="$MODPATH/payload/lib64"
fragment="$MODPATH/system/vendor/etc/vintf/manifest/android.hardware.camera.provider-service-vcam-v2.xml"
vendor_etc="$MODPATH/system/vendor/etc"
vintf_dir="$vendor_etc/vintf"
manifest_dir="$vintf_dir/manifest"
empty_config="$MODPATH/payload/empty-config"
camera_config="$MODPATH/payload/camera-config"
backend_manifest="$MODPATH/payload/backend.sha256"
streamer="$MODPATH/system/bin/vcam-streamer"
publisher="$MODPATH/system/bin/vcam-publisher"
daemon="$MODPATH/system/bin/vcamd"

for required in \
    "$binary" \
    "$libdir/libvcam_googlecamerahwl_impl.so" \
    "$libdir/libgooglecamerahal.so" \
    "$libdir/libgooglecamerahalutils.so" \
    "$libdir/lib_profiler.so" \
    "$libdir/libgrallocusage.so" \
    "$libdir/libprotobuf-cpp-full-21.7.so" \
    "$camera_config/emu_camera_back.json" \
    "$camera_config/emu_camera_front.json" \
    "$backend_manifest" \
    "$streamer" \
    "$publisher" \
    "$daemon" \
    "$fragment" \
    "$MODPATH/vcamctl" \
    "$MODPATH/provider-runner.sh" \
    "$MODPATH/device-probe.sh" \
    "$MODPATH/provider-control.sh" \
    "$MODPATH/post-fs-data.sh" \
    "$MODPATH/service.sh" \
    "$MODPATH/action.sh" \
    "$MODPATH/uninstall.sh"; do
    [ -f "$required" ] || abort "! Required probe file is missing: $required"
done

(cd "$MODPATH" && sha256sum -c payload/backend.sha256 >/dev/null) || \
    abort "! Backend payload checksum verification failed"

mkdir -p "$empty_config"
set_perm "$binary" 0 0 0755
set_perm_recursive "$libdir" 0 0 0755 0644
set_perm "$streamer" 0 2000 0755
set_perm "$publisher" 0 2000 0755
set_perm "$daemon" 0 0 0755
set_perm "$fragment" 0 0 0644
for config_dir in "$vendor_etc" "$vintf_dir" "$manifest_dir"; do
    chcon u:object_r:vendor_configs_file:s0 "$config_dir" || \
        abort "! Unable to label VINTF directory as vendor configuration: $config_dir"
done
chcon u:object_r:vendor_configs_file:s0 "$fragment" || \
    abort "! Unable to label the VINTF fragment as vendor configuration"
set_perm "$MODPATH/provider-control.sh" 0 0 0755
set_perm "$MODPATH/vcamctl" 0 0 0755
set_perm "$MODPATH/provider-runner.sh" 0 0 0755
set_perm "$MODPATH/device-probe.sh" 0 0 0755
set_perm "$MODPATH/post-fs-data.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755

ui_print "- Adds the checkvintf-qualified AIDL v2 vcam/0 declaration"
ui_print "- Preserves vendor_configs_file on the complete VINTF directory chain"
ui_print "- Registration runs in the background before CameraService starts"
if [ "$(cat "$MODPATH/profile.id" 2>/dev/null)" = nx769j-ukq1-20240417 ]; then
    ui_print "- Persistent Provider mode defaults to route"
    ui_print "- Healthy boots keep the unified module enabled"
else
    ui_print "- Boot defaults to zero cameras; arm-two is an explicit one-boot diagnostic"
    ui_print "- The following boot is disabled after the VINTF overlay is mounted"
fi
ui_print "- Includes the manager-independent image/video/RTSP backend"
ui_print "- Failure recovery uses a full reboot and never restarts cameraserver"
ui_print "- Reboot is required; KernelSU bootloop protection remains available"
