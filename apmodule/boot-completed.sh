#!/system/bin/sh

MODDIR=${0%/*}
LOG_DIR="/data/adb/android_vcam"
LOG_FILE="$LOG_DIR/health.log"
mkdir -p "$LOG_DIR"
chmod 0700 "$LOG_DIR"

echo "health-check $(date '+%Y-%m-%dT%H:%M:%S%z')" > "$LOG_FILE"

healthy=0
attempt=1
while [ "$attempt" -le 3 ]; do
    echo "attempt=$attempt" >> "$LOG_FILE"
    camera_dump="$(dumpsys media.camera 2>&1)"
    printf '%s\n' "$camera_dump" >> "$LOG_FILE"
    camera_count="$(printf '%s\n' "$camera_dump" | \
        sed -n 's/.*Number of camera devices: //p' | head -n 1)"
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
