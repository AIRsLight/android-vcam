#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR=/data/adb/android_vcam
LOG_FILE="$STATE_DIR/module.log"
mkdir -p "$STATE_DIR" /data/vendor/camera/vcam
chmod 0700 "$STATE_DIR"
chown camera:camera /data/vendor/camera/vcam
chmod 0770 /data/vendor/camera/vcam
chcon -R u:object_r:vcam_camera_data_file:s0 /data/vendor/camera/vcam \
    >/dev/null 2>&1 || restorecon -RF /data/vendor/camera/vcam

if [ -x "$MODDIR/device-probe.sh" ]; then
    "$MODDIR/device-probe.sh" "$STATE_DIR/device-profile.conf" >> "$LOG_FILE" 2>&1 || \
        echo "oneplus-global: device probe failed" >> "$LOG_FILE"
fi

[ ! -e "$MODDIR/disable" ] || exit 0
[ -f "$STATE_DIR/mount.ok" ] || {
    echo "oneplus-global: verified overlay is unavailable; camera stack left untouched" >> "$LOG_FILE"
    exit 0
}

DAEMON="$MODDIR/system/bin/vcamd"
DAEMON_PID="$STATE_DIR/vcamd.pid"
if [ -f "$DAEMON_PID" ]; then
    old_pid="$(cat "$DAEMON_PID" 2>/dev/null)"
    case "$old_pid" in
        ''|*[!0-9]*) ;;
        *) kill "$old_pid" 2>/dev/null ;;
    esac
fi
if [ -x "$DAEMON" ]; then
    nohup runcon u:r:vcamd:s0 "$DAEMON" "$MODDIR/vcamctl" \
        >> "$STATE_DIR/vcamd.log" 2>&1 &
    echo "$!" > "$DAEMON_PID"
    chmod 0600 "$DAEMON_PID"
fi

for provider in /data/adb/android_vcam/providers/*; do
    [ -f "$provider/meta" ] || continue
    [ -e "$provider/autostart" ] || continue
    "$MODDIR/vcamctl" provider-start "${provider##*/}" >/dev/null 2>&1
done

echo "oneplus-global: restarting camera stack" >> "$LOG_FILE"
setprop ctl.restart vendor.camera-provider-2-4
setprop ctl.restart cameraserver
