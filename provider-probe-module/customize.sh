#!/system/bin/sh

ui_print "- Validating manual Android 14 AIDL provider probe"

sdk="$(getprop ro.build.version.sdk)"
abi="$(getprop ro.product.cpu.abi)"
fingerprint="$(getprop ro.build.fingerprint)"
expected_fingerprint='nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys'

[ "$sdk" = "34" ] || abort "! This probe is restricted to Android 14"
[ "$abi" = "arm64-v8a" ] || abort "! This probe requires arm64-v8a"
[ "$fingerprint" = "$expected_fingerprint" ] || \
    abort "! This development probe is restricted to the qualified NX769J build"

binary="$MODPATH/payload/bin/android.hardware.camera.provider-service-vcam-v2"
client="$MODPATH/payload/bin/vcam_provider_probe_client"
libdir="$MODPATH/payload/lib64"
empty_config="$MODPATH/payload/empty-config"
camera_config="$MODPATH/payload/camera-config"

for required in \
    "$binary" \
    "$client" \
    "$libdir/libvcam_googlecamerahwl_impl.so" \
    "$libdir/libgooglecamerahal.so" \
    "$libdir/libgooglecamerahalutils.so" \
    "$libdir/lib_profiler.so" \
    "$libdir/libgrallocusage.so" \
    "$libdir/libprotobuf-cpp-full-21.7.so" \
    "$libdir/libprovider_probe_trace.so" \
    "$camera_config/emu_camera_back.json" \
    "$camera_config/emu_camera_front.json"; do
    [ -f "$required" ] || abort "! Required probe file is missing: $required"
done

mkdir -p "$empty_config"
set_perm "$binary" 0 0 0755
set_perm "$client" 0 0 0755
set_perm_recursive "$libdir" 0 0 0755 0644
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755

ui_print "- No system or vendor paths will be mounted"
ui_print "- No VINTF fragment or boot-time service is included"
ui_print "- Reboot once to load the narrowly scoped SELinux rule"
ui_print "- The action button starts the safe zero-camera probe"
ui_print "- ADB may opt into IDs 1000/1001 with ANDROID_VCAM_PROBE_ADVERTISE_CAMERAS=1"
