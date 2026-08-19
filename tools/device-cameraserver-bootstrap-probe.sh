#!/system/bin/sh

field() {
    key=$1
    shift
    value=$*
    [ -n "$value" ] || value=unknown
    printf '%s=%s\n' "$key" "$value"
}

first_line() {
    sed -n '1p'
}

stock=/system/bin/cameraserver
field schema 1
field sdk "$(getprop ro.build.version.sdk)"
field release "$(getprop ro.build.version.release)"
field fingerprint "$(getprop ro.build.fingerprint)"
field abi "$(getprop ro.product.cpu.abi)"
field enforcing "$(getenforce 2>/dev/null)"
field root_identity "$(id 2>/dev/null)"

if [ -e "$stock" ]; then
    field stock_realpath "$(readlink -f "$stock" 2>/dev/null)"
    field stock_stat "$(stat -c '%d:%i:%s:%a:%u:%g' "$stock" 2>/dev/null)"
    field stock_context "$(ls -Zd "$stock" 2>/dev/null | awk '{print $1}')"
    field stock_sha256 "$(sha256sum "$stock" 2>/dev/null | awk '{print $1}')"
else
    field stock_realpath missing
    field stock_stat missing
    field stock_context missing
    field stock_sha256 missing
fi

camera_pid=$(pidof cameraserver 2>/dev/null | awk '{print $1}')
field cameraserver_pid "$camera_pid"
if [ -n "$camera_pid" ]; then
    field cameraserver_context "$(cat "/proc/$camera_pid/attr/current" 2>/dev/null | tr -d '\000\r\n')"
    field cameraserver_exe "$(readlink -f "/proc/$camera_pid/exe" 2>/dev/null)"
    field cameraserver_cmdline "$(tr '\000' ' ' < "/proc/$camera_pid/cmdline" 2>/dev/null)"
fi

if [ -d /data/adb/ksu ]; then
    field root_manager kernelsu
elif [ -d /data/adb/ap ]; then
    field root_manager apatch
elif [ -d /data/adb/magisk ]; then
    field root_manager magisk
else
    field root_manager unknown
fi

field vcam_stock_collision "$(test -e /system/bin/vcam/cameraserver && echo yes || echo no)"
field vcam_launcher_collision "$(test -e /system/bin/vcam_cameraserver_launcher && echo yes || echo no)"
field router_collision "$(test -e /system/lib64/libvcam_cameraserver_router.so && echo yes || echo no)"
field bootstrap_mode "$(sed -n '1p' /data/vendor/camera/vcam/bootstrap.mode 2>/dev/null)"
field bootstrap_pending "$(test -e /data/vendor/camera/vcam/bootstrap.pending && echo yes || echo no)"

linker_config=unknown
for candidate in /linkerconfig/ld.config.txt /system/etc/ld.config.txt; do
    if [ -r "$candidate" ]; then
        linker_config=$candidate
        break
    fi
done
field linker_config "$linker_config"
if [ "$linker_config" != unknown ]; then
    field linker_system_lib64_mentions "$(grep -c '/system/lib64' "$linker_config" 2>/dev/null)"
    field linker_permitted_path_lines "$(grep -c '^namespace\..*\.permitted.paths' "$linker_config" 2>/dev/null)"
fi

field camera_service_count "$(service list 2>/dev/null | grep -c 'media.camera')"
field camera_mount "$(grep ' /system ' /proc/mounts 2>/dev/null | first_line)"
field module_mount_mentions "$(grep -c 'android_vcam' /proc/mounts 2>/dev/null)"
