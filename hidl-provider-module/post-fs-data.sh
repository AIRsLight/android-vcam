#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR=/data/adb/android_vcam_hidl_provider
WATCHDOG_LOG=$STATE_DIR/watchdog.log

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"

watch_boot_completion() {
    attempt=0
    while [ "$attempt" -lt 180 ]; do
        if [ "$(getprop sys.boot_completed)" = "1" ]; then
            echo "boot completed; watchdog disarmed" >> "$WATCHDOG_LOG"
            return 0
        fi
        sleep 1
        attempt=$((attempt + 1))
    done

    echo "boot incomplete after 180 seconds; disabling HIDL provider module" >> "$WATCHDOG_LOG"
    touch "$MODDIR/disable"
    sync
    setprop sys.powerctl reboot,vcam-hidl-recovery
}

watch_boot_completion &
exit 0
