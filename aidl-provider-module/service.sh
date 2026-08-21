#!/system/bin/sh

MODDIR=${0%/*}
PROBE_STATE_DIR=/data/adb/android_vcam_aidl_provider
BACKEND_STATE_DIR=/data/adb/android_vcam
BOOT_LOG=$PROBE_STATE_DIR/bootstrap.log
BACKEND_LOG=$BACKEND_STATE_DIR/module.log
DAEMON=$MODDIR/system/bin/vcamd
DAEMON_PID=$BACKEND_STATE_DIR/vcamd.pid
BACKEND_BOOT_ID_FILE=$PROBE_STATE_DIR/backend.boot-id
NETWORK_RETRY_ROUNDS=18
NETWORK_RETRY_DELAY_SECONDS=10

mkdir -p "$PROBE_STATE_DIR" "$BACKEND_STATE_DIR"
chmod 0700 "$PROBE_STATE_DIR" "$BACKEND_STATE_DIR"
current_boot_id=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)
if [ -n "$current_boot_id" ] && \
   [ "$(cat "$BACKEND_BOOT_ID_FILE" 2>/dev/null)" = "$current_boot_id" ]; then
    echo "backend lifecycle already started for boot=$current_boot_id" >> "$BOOT_LOG"
    exit 0
fi
printf '%s\n' "$current_boot_id" > "$BACKEND_BOOT_ID_FILE"
chmod 0600 "$BACKEND_BOOT_ID_FILE"
echo "backend lifecycle reached boot=${current_boot_id:-unknown}" >> "$BOOT_LOG"

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

retry_unpublished_providers_after_boot() {
    attempt=0
    while [ "$attempt" -lt 180 ]; do
        [ "$(getprop sys.boot_completed)" = "1" ] && break
        sleep 1
        attempt=$((attempt + 1))
    done
    [ "$(getprop sys.boot_completed)" = "1" ] || {
        echo "service: backend retry skipped; boot did not complete" >> "$BACKEND_LOG"
        return 0
    }
    # Give Android's network validation and route selection a short settling
    # interval after boot_completed before retrying failed remote providers.
    sleep 5
    round=1
    while [ "$round" -le "$NETWORK_RETRY_ROUNDS" ]; do
        pending=0
        for provider in "$BACKEND_STATE_DIR"/providers/*; do
            [ -f "$provider/meta" ] || continue
            [ -e "$provider/autostart" ] || continue
            source_type=$(sed -n 's/^type=//p' "$provider/meta")
            case "$source_type" in
                rtsp|http|https|hls) ;;
                *) continue ;;
            esac
            id=${provider##*/}
            enabled="/data/vendor/camera/vcam/providers/$id/enabled"
            [ -e "$enabled" ] && continue
            echo "service: retry-provider=$id round=$round/$NETWORK_RETRY_ROUNDS" \
                >> "$BACKEND_LOG"
            "$MODDIR/vcamctl" provider-start "$id" >> "$BACKEND_LOG" 2>&1
            [ -e "$enabled" ] || pending=1
        done
        if [ "$pending" -eq 0 ]; then
            echo "service: network-provider retry complete round=$round" \
                >> "$BACKEND_LOG"
            return 0
        fi
        [ "$round" -ge "$NETWORK_RETRY_ROUNDS" ] && break
        sleep "$NETWORK_RETRY_DELAY_SECONDS"
        round=$((round + 1))
    done
    echo "service: network-provider retry exhausted rounds=$NETWORK_RETRY_ROUNDS" \
        >> "$BACKEND_LOG"
}

retry_unpublished_providers_after_boot &
