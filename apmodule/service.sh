#!/system/bin/sh

MODDIR=${0%/*}
LOG_DIR="/data/adb/android_vcam"
LOG_FILE="$LOG_DIR/module.log"
mkdir -p "$LOG_DIR"
chmod 0700 "$LOG_DIR"
mkdir -p /data/vendor/camera/vcam
chown camera:camera /data/vendor/camera/vcam
chmod 0770 /data/vendor/camera/vcam
restorecon -RF /data/vendor/camera/vcam

# Capture the immutable platform/camera-stack facts before deciding which
# transport adapter can be used. The profile remains available even if the
# current device-specific mount is disabled.
if [ -x "$MODDIR/device-probe.sh" ]; then
    "$MODDIR/device-probe.sh" "$LOG_DIR/device-profile.conf" >> "$LOG_FILE" 2>&1 || \
        echo "service: device capability probe failed" >> "$LOG_FILE"
fi

if [ -e "$MODDIR/disable" ]; then
    echo "service: module disabled" >> "$LOG_FILE"
    exit 0
fi

if [ ! -f "$LOG_DIR/mount.ok" ]; then
    echo "service: HAL bind mount was not verified; camera stack left untouched" >> "$LOG_FILE"
    exit 0
fi

# The manager is intentionally root-free. This narrowly-scoped daemon accepts
# only a fixed controller command set and authenticates the app through
# SO_PEERCRED plus Android's package-to-UID database.
DAEMON="$MODDIR/system/bin/vcamd"
DAEMON_PID="$LOG_DIR/vcamd.pid"
if [ -f "$DAEMON_PID" ]; then
    old_pid=$(cat "$DAEMON_PID" 2>/dev/null)
    case "$old_pid" in
        ''|*[!0-9]*) ;;
        *)
            if tr '\000' ' ' < "/proc/$old_pid/cmdline" 2>/dev/null | grep -q '/vcamd'; then
                kill "$old_pid" 2>/dev/null
            fi
            ;;
    esac
fi
if [ -x "$DAEMON" ]; then
    nohup runcon u:r:vcamd:s0 "$DAEMON" "$MODDIR/vcamctl" >> "$LOG_DIR/vcamd.log" 2>&1 &
    echo "$!" > "$DAEMON_PID"
    chmod 0600 "$DAEMON_PID"
else
    echo "service: control daemon missing" >> "$LOG_FILE"
fi

# Resume saved remote/video providers. Each runner owns its provider-specific
# frame, cache, PID and log files.
for provider in /data/adb/android_vcam/providers/*; do
    [ -f "$provider/meta" ] || continue
    id=${provider##*/}
    if [ -e "/data/vendor/camera/vcam/providers/$id/enabled" ] && \
       [ ! -e "$provider/autostart" ]; then
        touch "$provider/autostart"
        chmod 0600 "$provider/autostart"
    fi
    [ -e "$provider/autostart" ] || continue
    "$MODDIR/vcamctl" provider-start "$id" >/dev/null 2>&1
done

# APatch mounts modules before this late_start service stage. Restarting the
# provider makes sure an early-boot process does not retain the original DSO.
echo "service: restarting camera stack $(date '+%Y-%m-%dT%H:%M:%S%z')" >> "$LOG_FILE"
setprop ctl.restart vendor.camera-provider-2-4
setprop ctl.restart cameraserver
