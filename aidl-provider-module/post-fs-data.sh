#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR=/data/adb/android_vcam_aidl_provider
WATCHDOG_LOG=$STATE_DIR/watchdog.log
BOOT_LOG=$STATE_DIR/bootstrap.log
MOUNTED_FRAGMENT=/vendor/etc/vintf/manifest/android.hardware.camera.provider-service-vcam-v2.xml
NEXT_BOOT_MODE_FILE=$STATE_DIR/next-boot.mode

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"
boot_mode="$(cat "$NEXT_BOOT_MODE_FILE" 2>/dev/null)"
rm -f "$NEXT_BOOT_MODE_FILE"
case "$boot_mode" in
    two|route) ;;
    *) boot_mode=zero ;;
esac
echo "bootstrap 0.5.0-dev.29 started mode=$boot_mode" >> "$BOOT_LOG"

disable_next_boot() {
    touch "$MODDIR/disable"
    sync
}

request_recovery_reboot() {
    echo "$1" >> "$WATCHDOG_LOG"
    disable_next_boot
    setprop sys.powerctl reboot,vcam-aidl-recovery
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

bootstrap_provider() {
    # Wait until MetaModule exposes this boot's fragment before writing the
    # next-boot disable marker. This avoids racing a metamodule that is still
    # deciding which module trees to mount.
    mounted=0
    attempt=0
    while [ "$attempt" -lt 50 ]; do
        if [ -f "$MOUNTED_FRAGMENT" ] && \
           grep -q '<instance>vcam/0</instance>' "$MOUNTED_FRAGMENT" 2>/dev/null; then
            mounted=1
            break
        fi
        sleep 0.1
        attempt=$((attempt + 1))
    done
    if [ "$mounted" != "1" ]; then
        request_recovery_reboot \
            "VINTF overlay was not visible during early boot; requesting disabled recovery reboot"
        return 1
    fi

    # The current overlay stays mounted after this point. A manual reboot or
    # power loss now returns to stock even if provider registration fails.
    disable_next_boot

    export ANDROID_VCAM_REGISTRATION_ATTEMPTS=10
    registered=0
    attempt=0
    while [ "$attempt" -lt 15 ]; do
        if sh "$MODDIR/provider-control.sh" "$MODDIR" "start-$boot_mode" >> "$BOOT_LOG" 2>&1; then
            registered=1
            break
        fi
        sleep 0.1
        attempt=$((attempt + 1))
    done

    if [ "$registered" != "1" ]; then
        request_recovery_reboot \
            "provider was not registered during early boot; requesting disabled recovery reboot"
        return 1
    fi

    echo "$boot_mode AIDL provider registered; next boot remains disabled" >> "$BOOT_LOG"
    # KernelSU skips late-start service.sh after this one-shot probe writes its
    # next-boot disable marker. Start the independent backend explicitly from
    # the still-running post-fs-data worker; service.sh has a boot-ID guard for
    # root managers that do invoke it later in the same boot.
    sh "$MODDIR/service.sh" >> "$BOOT_LOG" 2>&1
    watch_boot_completion
}

# Do not block KernelSU or MetaModule post-fs-data sequencing. servicemanager
# may reload VINTF immediately after the overlay is mounted, so the background
# bootstrap retries until the declared stable-AIDL instance can register.
bootstrap_provider &
exit 0
