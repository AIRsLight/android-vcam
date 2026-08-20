#!/system/bin/sh

MODDIR=${0%/*}
sh "$MODDIR/provider-control.sh" "$MODDIR" stop >/dev/null 2>&1
for provider in /data/adb/android_vcam/providers/*; do
    [ -f "$provider/meta" ] || continue
    "$MODDIR/vcamctl" provider-suspend "${provider##*/}" >/dev/null 2>&1
done
daemon_pid_file=/data/adb/android_vcam/vcamd.pid
if [ -s "$daemon_pid_file" ]; then
    daemon_pid=$(cat "$daemon_pid_file" 2>/dev/null)
    case "$daemon_pid" in ''|*[!0-9]*) ;;
        *)
            if [ "$(readlink "/proc/$daemon_pid/exe" 2>/dev/null)" = \
                 "$MODDIR/system/bin/vcamd" ]; then
                kill "$daemon_pid" 2>/dev/null
            fi
            ;;
    esac
fi
rm -f "$daemon_pid_file"
rm -rf /data/adb/android_vcam_aidl_provider
