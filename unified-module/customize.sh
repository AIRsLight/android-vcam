#!/system/bin/sh

ONEPLUS_FINGERPRINT='OnePlus/OnePlus7Pro_CH/OnePlus7Pro:12/SKQ1.211113.001/P.202303230244:user/release-keys'
NX769J_FINGERPRINT='nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys'
META_ROOT=/data/adb/metamodule
PROFILE_ROOT="$MODPATH/payload/profiles"

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

mark_disabled_legacy_module_for_removal() {
    legacy_id="$1"
    legacy_dir="/data/adb/modules/$legacy_id"
    [ -d "$legacy_dir" ] || return 0
    [ -e "$legacy_dir/disable" ] || \
        abort "! Disable legacy module $legacy_id and reboot before installing the unified module"
    touch "$legacy_dir/remove" || abort "! Unable to retire legacy module $legacy_id"
    ui_print "- Retiring disabled legacy module: $legacy_id"
}

install_profile_tree() {
    source_dir="$1"
    [ -d "$source_dir" ] || abort "! Selected profile payload is missing: $source_dir"
    cp -af "$source_dir/." "$MODPATH/" || abort "! Unable to install selected profile payload"
}

root_manager=unknown
[ "$KSU" = true ] && root_manager=KernelSU
[ "$APATCH" = true ] && root_manager=APatch
[ "$root_manager" != unknown ] || abort "! Only KernelSU and APatch are supported"

require_active_metamodule

fingerprint="$(getprop ro.build.fingerprint)"
abi="$(getprop ro.product.cpu.abi)"
[ "$abi" = arm64-v8a ] || abort "! This release requires arm64-v8a; found $abi"

case "$fingerprint" in
    "$ONEPLUS_FINGERPRINT")
        profile=oneplus7pro-p202303230244
        profile_name='OnePlus 7 Pro · Android 12'
        ;;
    "$NX769J_FINGERPRINT")
        profile=nx769j-ukq1-20240417
        profile_name='NX769J · Android 14'
        mark_disabled_legacy_module_for_removal android_vcam_aidl_provider
        mark_disabled_legacy_module_for_removal android_vcam_portable
        ;;
    *)
        abort "! Unsupported device build: $fingerprint"
        ;;
esac

ui_print "- Root manager: $root_manager"
ui_print "- Active MetaModule: $(sed -n 's/^name=//p' "$META_ROOT/module.prop" | head -n 1)"
ui_print "- Selected profile: $profile_name"

install_profile_tree "$PROFILE_ROOT/$profile"
rm -rf "$PROFILE_ROOT"
printf '%s\n' "$profile" > "$MODPATH/profile.id"

case "$profile" in
    oneplus7pro-p202303230244)
        . "$MODPATH/install-profile.sh"
        proxy_slot="$MODPATH/vendor/lib64/hw/local_time.default.so"
        [ -f "$proxy_slot" ] || abort "! MetaModule proxy slot payload is missing"
        set_perm "$proxy_slot" 0 0 0644 u:object_r:vendor_file:s0
        ;;
    nx769j-ukq1-20240417)
        . "$MODPATH/install-provider.sh"
        . "$MODPATH/install-router.sh"
        ;;
esac

for script in profile-service.sh provider-service.sh router-service.sh service.sh; do
    [ -f "$MODPATH/$script" ] && set_perm "$MODPATH/$script" 0 0 0755
done
set_perm "$MODPATH/customize.sh" 0 0 0755
set_perm "$MODPATH/profile.id" 0 0 0644
rm -f "$MODPATH/install-profile.sh" "$MODPATH/install-provider.sh" \
    "$MODPATH/install-router.sh"

description="Auto-selected $profile_name profile via $root_manager MetaModule."
sed -i "s|^description=.*|description=$description|" "$MODPATH/module.prop"
ui_print "- Installed as one module: android_vcam"
ui_print "- Reboot is required"
