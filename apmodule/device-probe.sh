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
aidl_instances="$(service check android.hardware.camera.provider.ICameraProvider/internal/0 2>/dev/null || true)"

transport=unknown
provider_version=unknown
provider_instance=unknown
case "$aidl_instances" in
    *': found')
        transport=aidl
        provider_version=1+
        provider_instance=internal/0
        ;;
esac
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

emit_profile() {
    field schema_version 1
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
    field adapter_hint "$adapter_hint"
    field legacy_module "${legacy_module:-none}"
    field legacy_module_hash "$legacy_module_hash"
    field cameraservice_hash "$cameraservice_hash"
    field camera_count "$camera_count"
    field camera_service_binder "$(printf '%s' "$camera_services" | grep -c 'media.camera')"
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
