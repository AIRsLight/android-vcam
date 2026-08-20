#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR=/data/adb/android_vcam_hidl_provider
WATCHDOG_LOG=$STATE_DIR/watchdog.log
BOOT_LOG=$STATE_DIR/bootstrap.log

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"

disable_next_boot() {
    touch "$MODDIR/disable"
    sync
}

request_recovery_reboot() {
    echo "$1" >> "$WATCHDOG_LOG"
    disable_next_boot
    setprop sys.powerctl reboot,vcam-hidl-recovery
}

watch_boot_completion() {
    attempt=0
    while [ "$attempt" -lt 180 ]; do
        if [ "$(getprop sys.boot_completed)" = "1" ]; then
            {
                echo "boot completed; watchdog disarmed"
                echo "cameraserver=$(pidof cameraserver 2>/dev/null)"
                sh "$MODDIR/provider-control.sh" "$MODDIR" status 2>/dev/null
            } >> "$WATCHDOG_LOG"
            return 0
        fi
        sleep 1
        attempt=$((attempt + 1))
    done

    request_recovery_reboot \
        "boot incomplete after 180 seconds; requesting disabled recovery reboot"
}

# A VINTF-declared HIDL provider is a blocking CameraService dependency. Start
# the zero-camera process during post-fs-data, before the HAL and cameraserver
# classes begin. Registration is retried briefly because hwservicemanager may
# still be reloading its VINTF cache after meta-overlayfs mounts the fragment.
registered=0
attempt=0
while [ "$attempt" -lt 20 ]; do
    if sh "$MODDIR/provider-control.sh" "$MODDIR" start-zero >> "$BOOT_LOG" 2>&1; then
        registered=1
        break
    fi
    sleep 0.1
    attempt=$((attempt + 1))
done

if [ "$registered" != "1" ]; then
    request_recovery_reboot \
        "provider was not registered during post-fs-data; requesting disabled recovery reboot"
    exit 1
fi

# This is deliberately a one-boot qualification. The current overlay and
# process stay active, while the marker guarantees the next boot is stock even
# after a manual reboot or power loss.
disable_next_boot
echo "zero-camera provider registered; module disabled for next boot" >> "$BOOT_LOG"

watch_boot_completion &
exit 0
