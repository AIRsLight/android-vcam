#!/system/bin/sh

MODDIR=${0%/*}
PROFILE="$(cat "$MODDIR/profile.id" 2>/dev/null)"
STATE_DIR=/data/adb/android_vcam
LOG_FILE=$STATE_DIR/module.log

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"

if [ -x "$MODDIR/device-probe.sh" ]; then
    "$MODDIR/device-probe.sh" "$STATE_DIR/device-profile.conf" >> "$LOG_FILE" 2>&1 || \
        echo "service: device capability probe failed" >> "$LOG_FILE"
fi

case "$PROFILE" in
    oneplus7pro-p202303230244)
        sh "$MODDIR/profile-service.sh"
        ;;
    nx769j-ukq1-20240417)
        export ANDROID_VCAM_CURRENT_BOOT_ACTIVE=1
        sh "$MODDIR/provider-service.sh"
        sh "$MODDIR/router-service.sh"
        ;;
    *)
        echo "service: invalid or missing installed profile: $PROFILE" >> "$LOG_FILE"
        touch "$MODDIR/disable"
        exit 1
        ;;
esac
