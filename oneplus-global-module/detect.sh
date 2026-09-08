#!/system/bin/sh

# Read-only eligibility probe, not a claim of hardware qualification. Match the
# physical provider executable, never an OEM virtual-camera service name.
detect_oneplus_global() {
    detected_provider_service=
    detected_brand="$(getprop ro.product.brand | tr '[:upper:]' '[:lower:]')"
    detected_manufacturer="$(getprop ro.product.manufacturer | tr '[:upper:]' '[:lower:]')"
    [ "$detected_brand" = oneplus ] || [ "$detected_manufacturer" = oneplus ] || return 1
    case "$(getprop ro.build.version.sdk)" in
        29|30|31|32|33|34) ;;
        *) return 1 ;;
    esac
    [ "$(getprop ro.product.cpu.abi)" = arm64-v8a ] || return 1
    [ -f /vendor/lib64/hw/camera.qcom.so ] || return 1
    [ -f /vendor/lib64/hw/local_time.default.so ] || return 1
    provider_executable=/vendor/bin/hw/android.hardware.camera.provider@2.4-service_64
    [ -f "$provider_executable" ] || return 1
    provider_prefix="$(od -An -N 6 -t x1 "$provider_executable" 2>/dev/null | tr -d '[:space:]')"
    provider_machine="$(od -An -j 18 -N 2 -t u2 "$provider_executable" 2>/dev/null | tr -d '[:space:]')"
    [ "$provider_prefix" = 7f454c460201 ] && [ "$provider_machine" = 183 ] || return 1
    detected_provider_service="$(awk -v executable="$provider_executable" \
        '$1 == "service" && $3 == executable { print $2 }' \
        /vendor/etc/init/*.rc /odm/etc/init/*.rc 2>/dev/null | sort -u)"
    case "$detected_provider_service" in
        ''|*[!a-zA-Z0-9_.@-]*) return 1 ;;
    esac
    return 0
}
