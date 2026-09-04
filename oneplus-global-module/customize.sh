#!/system/bin/sh

META_ROOT=/data/adb/metamodule
HAL_PATH=/vendor/lib64/hw/camera.qcom.so
SNAPSHOT_SLOT=/vendor/lib64/hw/local_time.default.so
MODULE_HAL="$MODPATH/system/vendor/lib64/hw/camera.qcom.so"
MODULE_SNAPSHOT="$MODPATH/system/vendor/lib64/hw/local_time.default.so"
PROFILE_FILE="$MODPATH/oneplus-profile.conf"
INSTALLED_MODULE=/data/adb/modules/android_vcam_oneplus_global

require_active_metamodule() {
    [ -d "$META_ROOT" ] || \
        abort "! Install and activate a MetaModule supported by the current root manager first"
    [ ! -e "$META_ROOT/disable" ] || abort "! The active MetaModule is disabled"
    meta_flag="$(sed -n 's/^metamodule=//p' "$META_ROOT/module.prop" 2>/dev/null | head -n 1)"
    case "$meta_flag" in
        1|true) ;;
        *) abort "! /data/adb/metamodule is not an active MetaModule" ;;
    esac
}

root_manager=unknown
[ "$KSU" = true ] && root_manager=KernelSU
[ "$APATCH" = true ] && root_manager=APatch
[ "$root_manager" != unknown ] || abort "! Only KernelSU and APatch are supported"
require_active_metamodule

manufacturer="$(getprop ro.product.manufacturer | tr '[:upper:]' '[:lower:]')"
brand="$(getprop ro.product.brand | tr '[:upper:]' '[:lower:]')"
case "$manufacturer:$brand" in
    *oneplus*) ;;
    *) abort "! This adapter is restricted to OnePlus firmware" ;;
esac

sdk="$(getprop ro.build.version.sdk)"
case "$sdk" in
    29|30|31|32|33|34) ;;
    *) abort "! Supported Android API range is 29-34; found $sdk" ;;
esac
[ "$(getprop ro.product.cpu.abi)" = arm64-v8a ] || \
    abort "! The OnePlus adapter requires an arm64-v8a device"

for required in "$HAL_PATH" "$SNAPSHOT_SLOT" "$MODULE_HAL"; do
    [ -f "$required" ] || abort "! Required camera file is missing: $required"
done

elf_class="$(od -An -j 4 -N 1 -t u1 "$HAL_PATH" 2>/dev/null | tr -d '[:space:]')"
elf_machine="$(od -An -j 18 -N 2 -t u2 "$HAL_PATH" 2>/dev/null | tr -d '[:space:]')"
[ "$elf_class" = 2 ] && [ "$elf_machine" = 183 ] || \
    abort "! camera.qcom.so is not an AArch64 ELF64 module"
[ "$(wc -c < "$HAL_PATH")" -gt 65536 ] || abort "! camera.qcom.so is unexpectedly small"

# During an in-place update the currently mounted camera.qcom.so can be our old
# shim. Preserve the previous device-local snapshot instead of recursively
# snapshotting the shim. An active unified module must be removed first because
# two camera overlays cannot be ordered safely.
if [ -d /data/adb/modules/android_vcam ] && \
   [ ! -e /data/adb/modules/android_vcam/disable ]; then
    abort "! Disable the installed android_vcam module and reboot before changing adapters"
fi

snapshot_source="$HAL_PATH"
if [ -f "$INSTALLED_MODULE/system/vendor/lib64/hw/camera.qcom.so" ] && \
   [ -f "$INSTALLED_MODULE/system/vendor/lib64/hw/local_time.default.so" ]; then
    mounted_hash="$(sha256sum "$HAL_PATH" | awk '{print $1}')"
    installed_shim_hash="$(sha256sum \
        "$INSTALLED_MODULE/system/vendor/lib64/hw/camera.qcom.so" | awk '{print $1}')"
    if [ "$mounted_hash" = "$installed_shim_hash" ]; then
        snapshot_source="$INSTALLED_MODULE/system/vendor/lib64/hw/local_time.default.so"
        ui_print "- Preserving the existing OEM Camera HAL snapshot"
    fi
fi

mkdir -p "${MODULE_SNAPSHOT%/*}" || abort "! Unable to create snapshot directory"
cp -p "$snapshot_source" "$MODULE_SNAPSHOT" || abort "! Unable to snapshot the OEM Camera HAL"
snapshot_hash="$(sha256sum "$MODULE_SNAPSHOT" | awk '{print $1}')"
shim_hash="$(sha256sum "$MODULE_HAL" | awk '{print $1}')"
[ "$snapshot_hash" != "$shim_hash" ] || abort "! Refusing a recursive camera shim snapshot"

cat > "$PROFILE_FILE" <<EOF
schema=1
adapter=oneplus-qcom-global-shim
route_scope=global
sdk=$sdk
fingerprint=$(getprop ro.build.fingerprint)
original_camera_hal_sha256=$snapshot_hash
shim_sha256=$shim_hash
EOF

set_perm "$MODULE_HAL" 0 0 0644 u:object_r:vendor_file:s0
set_perm "$MODULE_SNAPSHOT" 0 0 0644 u:object_r:vendor_file:s0
set_perm "$PROFILE_FILE" 0 0 0644
for executable in post-mount.sh service.sh boot-completed.sh action.sh vcamctl \
                  provider-runner.sh device-probe.sh tls-ca.sh; do
    [ -f "$MODPATH/$executable" ] && set_perm "$MODPATH/$executable" 0 0 0755
done
set_perm "$MODPATH/system/vendor/bin/vcam-publisher" 0 0 0755 u:object_r:vendor_file:s0
set_perm "$MODPATH/system/bin/vcam-streamer" 0 2000 0755 u:object_r:system_file:s0
set_perm "$MODPATH/system/bin/vcamd" 0 0 0755 u:object_r:system_file:s0
set_perm "$MODPATH/system/framework/vcam-https-downloader.jar" 0 0 0644 u:object_r:system_file:s0

ui_print "- Selected OnePlus global adapter for API $sdk"
ui_print "- The original Camera HAL was copied locally; it is not part of the release archive"
ui_print "- Reboot is required"
