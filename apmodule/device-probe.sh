#!/system/bin/sh

# Produces a transport-neutral device profile used to select an AOSP or OEM
# camera adapter. Values are deliberately line-oriented so both shell code and
# the root-free manager can consume them without a JSON parser.

OUTPUT="$1"
TEMPORARY=""

clean_value() {
    printf '%s' "$1" | tr '\r\n\t' '   '
}

field() {
    printf '%s=%s\n' "$1" "$(clean_value "$2")"
}

prop() {
    getprop "$1" 2>/dev/null
}

camera_lshal="$(lshal 2>/dev/null | grep 'android.hardware.camera.provider@' || true)"
camera_services="$(service list 2>/dev/null | grep -i camera || true)"
aidl_service_line="$(printf '%s\n' "$camera_services" | sed -n \
    '/android\.hardware\.camera\.provider\.ICameraProvider\//{p;q;}')"

transport=unknown
provider_version=unknown
provider_instance=unknown
if [ -n "$aidl_service_line" ]; then
    provider_instance="$(printf '%s\n' "$aidl_service_line" | sed -n \
        's#.*android\.hardware\.camera\.provider\.ICameraProvider/\([^: ]*\):.*#\1#p')"
    [ -n "$provider_instance" ] || provider_instance=unknown
    transport=aidl
    provider_version=1+
    for manifest in \
        /vendor/etc/vintf/manifest.xml \
        /vendor/etc/vintf/manifest_*.xml \
        /vendor/etc/vintf/manifest/*.xml \
        /odm/etc/vintf/manifest.xml \
        /odm/etc/vintf/manifest/*.xml; do
        [ -r "$manifest" ] || continue
        grep -q '<name>android.hardware.camera.provider</name>' "$manifest" || continue
        grep -q "<instance>$provider_instance</instance>" "$manifest" || continue
        manifest_version="$(sed -n \
            '/<name>android.hardware.camera.provider<\/name>/,/<\/hal>/ {
                s/.*<version>\([^<]*\)<\/version>.*/\1/p
            }' "$manifest" | head -n 1)"
        if [ -n "$manifest_version" ]; then
            provider_version="$manifest_version"
            break
        fi
    done
fi
if [ "$transport" = unknown ] && [ -n "$camera_lshal" ]; then
    transport=hidl
    provider_version="$(printf '%s\n' "$camera_lshal" | sed -n \
        's/.*android\.hardware\.camera\.provider@\([0-9.]*\)::ICameraProvider.*/\1/p' | \
        head -n 1)"
    provider_instance="$(printf '%s\n' "$camera_lshal" | sed -n \
        's#.*::ICameraProvider/\([^ ]*\).*#\1#p' | head -n 1)"
    [ -n "$provider_version" ] || provider_version=unknown
    [ -n "$provider_instance" ] || provider_instance=unknown
fi

legacy_module=""
for candidate in \
    /vendor/lib64/hw/camera.*.so \
    /odm/lib64/hw/camera.*.so \
    /system/vendor/lib64/hw/camera.*.so; do
    [ -f "$candidate" ] || continue
    case "$candidate" in *camera.vcam.so) continue ;; esac
    legacy_module="$candidate"
    break
done

provider_service=""
for candidate in \
    /vendor/bin/hw/*camera*provider* \
    /vendor/bin/*camera*provider* \
    /odm/bin/hw/*camera*provider*; do
    [ -f "$candidate" ] || continue
    case "$candidate" in *vcam*) continue ;; esac
    provider_service="$candidate"
    break
done

adapter_hint=unsupported
if [ "$transport" = aidl ]; then
    adapter_hint=aosp-aidl
elif [ "$transport" = hidl ] && [ -n "$legacy_module" ]; then
    adapter_hint=legacy-camera-module
elif [ "$transport" = hidl ]; then
    adapter_hint=hidl-provider-service
fi

camera_count="$(dumpsys media.camera 2>/dev/null | \
    sed -n 's/.*Number of camera devices: //p' | head -n 1)"
[ -n "$camera_count" ] || camera_count=unknown

cameraservice_hash=unknown
if [ -r /system/lib64/libcameraservice.so ]; then
    cameraservice_hash="$(sha256sum /system/lib64/libcameraservice.so 2>/dev/null | awk '{print $1}')"
fi
legacy_module_hash=none
if [ -n "$legacy_module" ]; then
    legacy_module_hash="$(sha256sum "$legacy_module" 2>/dev/null | awk '{print $1}')"
fi
provider_service_hash=none
if [ -n "$provider_service" ]; then
    provider_service_hash="$(sha256sum "$provider_service" 2>/dev/null | awk '{print $1}')"
fi

root_manager=none
root_context="$(id -Z 2>/dev/null)"
case "$root_context" in
    *:ksu:*) root_manager=ksu ;;
    *:apatch:*|*:magisk:*)
        if [ -d /data/adb/ap ]; then root_manager=apatch; else root_manager=magisk; fi
        ;;
esac

emit_profile() {
    field schema_version 2
    field sdk "$(prop ro.build.version.sdk)"
    field release "$(prop ro.build.version.release)"
    field abi "$(prop ro.product.cpu.abi)"
    field fingerprint "$(prop ro.build.fingerprint)"
    field manufacturer "$(prop ro.product.manufacturer)"
    field product_device "$(prop ro.product.device)"
    field board_platform "$(prop ro.board.platform)"
    field hardware "$(prop ro.hardware)"
    field selinux "$(getenforce 2>/dev/null)"
    field provider_transport "$transport"
    field provider_version "$provider_version"
    field provider_instance "$provider_instance"
    field provider_service "${provider_service:-none}"
    field provider_service_hash "$provider_service_hash"
    field adapter_hint "$adapter_hint"
    field legacy_module "${legacy_module:-none}"
    field legacy_module_hash "$legacy_module_hash"
    field cameraservice_hash "$cameraservice_hash"
    field camera_count "$camera_count"
    field camera_service_binder "$(printf '%s' "$camera_services" | grep -c 'media.camera')"
    field probe_uid "$(id -u 2>/dev/null)"
    field root_manager "$root_manager"
}

if [ -n "$OUTPUT" ]; then
    directory=${OUTPUT%/*}
    [ "$directory" != "$OUTPUT" ] || directory=.
    mkdir -p "$directory" || exit 1
    TEMPORARY="$OUTPUT.tmp.$$"
    emit_profile > "$TEMPORARY" || { rm -f "$TEMPORARY"; exit 1; }
    chmod 0600 "$TEMPORARY"
    mv -f "$TEMPORARY" "$OUTPUT"
else
    emit_profile
fi
