#!/system/bin/sh

MODDIR=${0%/*}
LOG_DIR="/data/adb/android_vcam"
LOG_FILE="$LOG_DIR/health.log"
mkdir -p "$LOG_DIR"
chmod 0700 "$LOG_DIR"

echo "health-check $(date '+%Y-%m-%dT%H:%M:%S%z')" > "$LOG_FILE"

healthy=0
attempt=1
camera_dump_file="$LOG_DIR/camera-dump.txt"
while [ "$attempt" -le 3 ]; do
    echo "attempt=$attempt" >> "$LOG_FILE"
    dumpsys media.camera > "$camera_dump_file" 2>&1
    chmod 0600 "$camera_dump_file"
    camera_count="$(sed -n 's/.*Number of camera devices: //p' \
        "$camera_dump_file" | head -n 1)"
    echo "camera-count=${camera_count:-unavailable}" >> "$LOG_FILE"
    if [ -e "$LOG_DIR/mount.ok" ] && \
       [ "${camera_count:-0}" -ge 2 ] 2>/dev/null; then
        healthy=1
        break
    fi
    attempt=$((attempt + 1))
    sleep 3
done

if [ "$healthy" -ne 1 ]; then
    echo "Camera health check failed; disabling module for the next boot" >> "$LOG_FILE"
    touch "$MODDIR/disable"
else
    echo "Camera health check passed" >> "$LOG_FILE"
fi

# late_start can run before Wi-Fi has a route. Retry providers that the user
# left enabled but which could not publish a frame during early boot.
for provider in /data/adb/android_vcam/providers/*; do
    [ -f "$provider/meta" ] || continue
    [ -e "$provider/autostart" ] || continue
    id=${provider##*/}
    [ -e "/data/vendor/camera/vcam/providers/$id/enabled" ] && continue
    echo "retry-provider=$id" >> "$LOG_FILE"
    "$MODDIR/vcamctl" provider-start "$id" >> "$LOG_FILE" 2>&1
done
