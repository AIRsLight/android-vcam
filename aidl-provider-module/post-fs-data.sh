#!/system/bin/sh

MODDIR=${0%/*}
if [ "$(cat "$MODDIR/profile.id" 2>/dev/null)" = nx769j-ukq1-20240417 ]; then
    UNIFIED_MODULE=1
    STATE_DIR=/data/adb/android_vcam/runtime/aidl
    ROUTER_STATE_DIR=/data/adb/android_vcam/runtime/router
else
    UNIFIED_MODULE=0
    STATE_DIR=/data/adb/android_vcam_aidl_provider
    ROUTER_STATE_DIR=/data/adb/android_vcam_portable
fi
WATCHDOG_LOG=$STATE_DIR/watchdog.log
BOOT_LOG=$STATE_DIR/bootstrap.log
MOUNTED_FRAGMENT=/vendor/etc/vintf/manifest/android.hardware.camera.provider-service-vcam-v2.xml
NEXT_BOOT_MODE_FILE=$STATE_DIR/next-boot.mode
CONFIGURED_MODE_FILE=$STATE_DIR/configured.mode
ROUTER_READY_FILE=$ROUTER_STATE_DIR/post-mount.boot-id

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"
boot_mode="$(cat "$NEXT_BOOT_MODE_FILE" 2>/dev/null)"
rm -f "$NEXT_BOOT_MODE_FILE"
if [ -z "$boot_mode" ] && [ "$UNIFIED_MODULE" = 1 ]; then
    boot_mode="$(cat "$CONFIGURED_MODE_FILE" 2>/dev/null)"
fi
case "$boot_mode" in
    zero|two|route) ;;
    *)
        if [ "$UNIFIED_MODULE" = 1 ]; then boot_mode=route; else boot_mode=zero; fi
        ;;
esac
echo "bootstrap 0.5.0-dev.39 started mode=$boot_mode unified=$UNIFIED_MODULE" >> "$BOOT_LOG"

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

    # In the unified module, Provider and CameraService routing share one
    # disable marker. Wait until post-mount has verified the router before
    # arming next-boot rollback, otherwise KernelSU could skip that hook.
    if [ "$(cat "$MODDIR/profile.id" 2>/dev/null)" = nx769j-ukq1-20240417 ]; then
        current_boot_id="$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)"
        router_ready=0
        attempt=0
        while [ "$attempt" -lt 50 ]; do
            if [ -n "$current_boot_id" ] && \
               [ "$(cat "$ROUTER_READY_FILE" 2>/dev/null)" = "$current_boot_id" ]; then
                router_ready=1
                break
            fi
            sleep 0.1
            attempt=$((attempt + 1))
        done
        if [ "$router_ready" != "1" ]; then
            request_recovery_reboot \
                "CameraService router was not verified before rollback arming"
            return 1
        fi
    fi

    # Standalone engineering probes remain one-shot. The unified production
    # module stays enabled after a healthy boot and disables itself only from
    # the recovery paths above/below.
    [ "$UNIFIED_MODULE" = 1 ] || disable_next_boot

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

    if [ "$UNIFIED_MODULE" = 1 ]; then
        echo "$boot_mode AIDL provider registered; unified module remains enabled" >> "$BOOT_LOG"
    else
        echo "$boot_mode AIDL provider registered; next boot remains disabled" >> "$BOOT_LOG"
    fi
    # Start the independent backend from this already-qualified early worker.
    # service.sh has a boot-ID guard when the root manager invokes it again.
    sh "$MODDIR/service.sh" >> "$BOOT_LOG" 2>&1
    watch_boot_completion
}

# Do not block KernelSU or MetaModule post-fs-data sequencing. servicemanager
# may reload VINTF immediately after the overlay is mounted, so the background
# bootstrap retries until the declared stable-AIDL instance can register.
bootstrap_provider &
exit 0
