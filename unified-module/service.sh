#!/system/bin/sh

MODDIR=${0%/*}
PROFILE="$(cat "$MODDIR/profile.id" 2>/dev/null)"
STATE_DIR=/data/adb/android_vcam
LOG_FILE=$STATE_DIR/module.log

if [ -z "$PROFILE" ]; then
    case "$(getprop ro.build.fingerprint)" in
        'OnePlus/OnePlus7Pro_CH/OnePlus7Pro:12/SKQ1.211113.001/P.202303230244:user/release-keys')
            PROFILE=oneplus7pro-p202303230244
            ;;
        'nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys')
            PROFILE=nx769j-ukq1-20240417
            ;;
    esac
fi

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"

if [ ! -s "$MODDIR/profile.id" ] && [ -n "$PROFILE" ]; then
    echo "service: recovered profile from exact fingerprint: $PROFILE" >> "$LOG_FILE"
fi

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
