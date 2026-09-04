#!/system/bin/sh

ui_print "- Validating read-only Android 14 compatibility probe"

sdk="$(getprop ro.build.version.sdk)"
abi="$(getprop ro.product.cpu.abi)"

[ "$sdk" = "34" ] || abort "! This probe is restricted to Android 14"
case "$abi" in
    arm64-v8a|x86_64) ;;
    *) abort "! Unsupported ABI: $abi" ;;
esac

for required in device-probe.sh run-probe.sh service.sh action.sh uninstall.sh skip_mount; do
    [ -f "$MODPATH/$required" ] || abort "! Required file is missing: $required"
done

# This package must remain incapable of overlaying Android partitions or
# changing SELinux policy. Keep the checks here as a second line of defence in
# addition to the host-side archive test.
for forbidden in system vendor odm product system_ext sepolicy.rule post-fs-data.sh post-mount.sh; do
    [ ! -e "$MODPATH/$forbidden" ] || abort "! Unsafe probe payload: $forbidden"
done

set_perm "$MODPATH/device-probe.sh" 0 0 0755
set_perm "$MODPATH/run-probe.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755

ui_print "- Report-only module installed"
ui_print "- Camera routing remains disabled"
