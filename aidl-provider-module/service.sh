#!/system/bin/sh

MODDIR=${0%/*}
PROBE_STATE_DIR=/data/adb/android_vcam_aidl_provider
BACKEND_STATE_DIR=/data/adb/android_vcam
BOOT_LOG=$PROBE_STATE_DIR/bootstrap.log
BACKEND_LOG=$BACKEND_STATE_DIR/module.log
DAEMON=$MODDIR/system/bin/vcamd
DAEMON_PID=$BACKEND_STATE_DIR/vcamd.pid

mkdir -p "$PROBE_STATE_DIR" "$BACKEND_STATE_DIR"
chmod 0700 "$PROBE_STATE_DIR" "$BACKEND_STATE_DIR"
echo "service stage reached; post-fs-data owns AIDL provider lifecycle" >> "$BOOT_LOG"

mkdir -p /data/vendor/camera/vcam/providers
chown camera:camera /data/vendor/camera/vcam /data/vendor/camera/vcam/providers
chmod 0770 /data/vendor/camera/vcam /data/vendor/camera/vcam/providers
restorecon /data/vendor/camera/vcam /data/vendor/camera/vcam/providers >/dev/null 2>&1

stop_owned_daemon() {
    [ -s "$DAEMON_PID" ] || return 0
    old_pid=$(cat "$DAEMON_PID" 2>/dev/null)
    case "$old_pid" in ''|*[!0-9]*) rm -f "$DAEMON_PID"; return 0 ;; esac
    if [ -d "/proc/$old_pid" ] && \
       [ "$(readlink "/proc/$old_pid/exe" 2>/dev/null)" = "$DAEMON" ]; then
        kill "$old_pid" 2>/dev/null
    fi
    rm -f "$DAEMON_PID"
}

export ANDROID_VCAM_CURRENT_BOOT_ACTIVE=1
stop_owned_daemon
if [ -x "$DAEMON" ] && [ -x "$MODDIR/vcamctl" ]; then
    nohup "$DAEMON" "$MODDIR/vcamctl" >> "$BACKEND_STATE_DIR/vcamd.log" 2>&1 &
    echo "$!" > "$DAEMON_PID"
    chmod 0600 "$DAEMON_PID"
    echo "service: AIDL backend daemon started pid=$!" >> "$BACKEND_LOG"
else
    echo "service: AIDL backend payload missing" >> "$BACKEND_LOG"
fi

# Configuration belongs to the backend, not the manager APK. Resume every
# provider that was explicitly left in autostart state without touching the
# CameraService or the already-registered VCAM provider process.
for provider in "$BACKEND_STATE_DIR"/providers/*; do
    [ -f "$provider/meta" ] || continue
    [ -e "$provider/autostart" ] || continue
    id=${provider##*/}
    "$MODDIR/vcamctl" provider-start "$id" >> "$BACKEND_LOG" 2>&1
done
