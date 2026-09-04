#!/system/bin/sh

ui_print "- Validating portable cameraserver bootstrap"

sdk="$(getprop ro.build.version.sdk)"
abi="$(getprop ro.product.cpu.abi)"
fingerprint="$(getprop ro.build.fingerprint)"
target_profile="$(cat "$MODPATH/profile.id" 2>/dev/null)"
case "$target_profile" in
    aosp14-avd-api34-ue1a-r23)
        expected_abi=x86_64
        expected_fingerprint='Android/sdk_phone64_x86_64/emu64x:14/UE1A.230829.036.A1/11228894:userdebug/test-keys'
        expected_cameraservice_hash='52fa175391f4bc753e5cddd6d541ceff4b4c83dd657aa0cc1e6edbe8deaec751'
        expected_camera_client_hash='8869bad7a6fb174b00de3258ef131122c2d7d53cc895f1df072d149ef7a28e54'
        ;;
    '')
        expected_abi=arm64-v8a
        expected_fingerprint='nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys'
        expected_cameraservice_hash='a26f8ee10002769428871e042c7993e87ad769703897dd75a2fb93a725c64438'
        expected_camera_client_hash='1cf518e86a2e5461e585d8dbd7a1dbc93e7ba2bcc95c3e254ebdcc72ee0433c5'
        ;;
    *) abort "! Unknown portable target profile: $target_profile" ;;
esac
[ "$sdk" = "34" ] || abort "! This prototype package is restricted to Android 14"
[ "$abi" = "$expected_abi" ] || \
    abort "! This prototype package requires $expected_abi"
[ "$fingerprint" = "$expected_fingerprint" ] || \
    abort "! This router is restricted to its packaged target build"

cameraservice_hash="$(sha256sum /system/lib64/libcameraservice.so 2>/dev/null | awk '{print $1}')"
camera_client_hash="$(sha256sum /system/lib64/libcamera_client.so 2>/dev/null | awk '{print $1}')"
[ "$cameraservice_hash" = "$expected_cameraservice_hash" ] || \
    abort "! CameraService ABI mismatch: $cameraservice_hash"
[ "$camera_client_hash" = "$expected_camera_client_hash" ] || \
    abort "! libcamera_client ABI mismatch: $camera_client_hash"
[ -d /data/adb/metamodule ] || \
    abort "! An active MetaModule must be installed first"
[ ! -e /data/adb/metamodule/disable ] || \
    abort "! The active MetaModule is disabled"
meta_flag="$(sed -n 's/^metamodule=//p' /data/adb/metamodule/module.prop 2>/dev/null | head -n 1)"
case "$meta_flag" in
    1|true) ;;
    *) abort "! /data/adb/metamodule is not a valid MetaModule" ;;
esac

live_cameraserver="/system/bin/cameraserver"
launcher="$MODPATH/system/bin/cameraserver"
stock="$MODPATH/system/bin/vcam/cameraserver"
router="$MODPATH/system/lib64/libvcam_cameraserver_router.so"
mode_file="$MODPATH/system/etc/android_vcam/bootstrap.mode"

for required in "$live_cameraserver" "$launcher" "$router" "$mode_file"; do
    [ -f "$required" ] || abort "! Required file is missing: $required"
done

mkdir -p "${stock%/*}"
launcher_hash="$(sha256sum "$launcher" | awk '{print $1}')"
live_hash="$(sha256sum "$live_cameraserver" | awk '{print $1}')"
stock_source="$live_cameraserver"
installed_launcher_hash=""
if [ -r "/data/adb/modules/$MODID/launcher.sha256" ]; then
    installed_launcher_hash="$(awk 'NR == 1 {print $1}' \
        "/data/adb/modules/$MODID/launcher.sha256")"
fi

if [ "$live_hash" = "$launcher_hash" ] || \
   { [ -n "$installed_launcher_hash" ] && \
     [ "$live_hash" = "$installed_launcher_hash" ]; }; then
    stock_source=""
    for candidate in \
        "/data/adb/metamodule/mnt/$MODID/system/bin/vcam/cameraserver" \
        "/data/adb/modules/$MODID/system/bin/vcam/cameraserver"; do
        if [ -f "$candidate" ] && \
           [ "$(sha256sum "$candidate" | awk '{print $1}')" != "$launcher_hash" ]; then
            stock_source="$candidate"
            break
        fi
    done
    [ -n "$stock_source" ] || abort "! Existing stock cameraserver cannot be recovered"
fi

cp -fp "$stock_source" "$stock" || abort "! Unable to capture stock cameraserver"
stock_hash="$(sha256sum "$stock" | awk '{print $1}')"
[ "$stock_hash" != "$launcher_hash" ] || abort "! Captured stock aliases the launcher"

printf '%s  %s\n' "$stock_hash" "/system/bin/vcam/cameraserver" > "$MODPATH/stock.sha256"
printf '%s  %s\n' "$launcher_hash" "/system/bin/cameraserver" > "$MODPATH/launcher.sha256"

set_perm "$launcher" 0 2000 0755
set_perm "$stock" 0 2000 0755
set_perm "$router" 0 0 0644
set_perm "$mode_file" 0 0 0644
set_perm "$MODPATH/post-mount.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/camera-map.sh" 0 0 0755
chcon u:object_r:cameraserver_exec:s0 "$launcher" "$stock" || \
    abort "! Unable to label cameraserver executables"
chcon u:object_r:system_file:s0 "$router" "$mode_file" || \
    abort "! Unable to label router configuration"

ui_print "- Captured stock cameraserver: $stock_hash"
ui_print "- Packaged bootstrap mode: $(sed -n '1p' "$mode_file")"
