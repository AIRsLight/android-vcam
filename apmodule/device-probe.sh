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
provider_manifest=none
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
            provider_manifest="$manifest"
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

provider_init_service=unknown
if [ -n "$provider_service" ]; then
    for init_rc in /vendor/etc/init/*.rc /odm/etc/init/*.rc; do
        [ -r "$init_rc" ] || continue
        init_service="$(awk -v binary="$provider_service" \
            '$1 == "service" && $3 == binary { print $2; exit }' "$init_rc")"
        if [ -n "$init_service" ]; then
            provider_init_service="$init_service"
            break
        fi
    done
fi

provider_service_context=unknown
provider_process_context=unknown
if [ -n "$provider_service" ]; then
    provider_service_context="$(ls -Z "$provider_service" 2>/dev/null | \
        awk 'NR == 1 { print $1 }')"
    provider_process_context="$(ps -AZ 2>/dev/null | awk \
        -v executable="${provider_service##*/}" '$NF == executable { print $1; exit }')"
    case "$provider_service_context" in
        u:object_r:*:s0) ;;
        *) provider_service_context=unknown ;;
    esac
    [ -n "$provider_process_context" ] || provider_process_context=unknown
fi

adapter_hint=unsupported
if [ "$transport" = aidl ]; then
    adapter_hint=aosp-aidl
elif [ "$transport" = hidl ] && [ -n "$legacy_module" ]; then
    adapter_hint=legacy-camera-module
elif [ "$transport" = hidl ]; then
    adapter_hint=hidl-provider-service
fi

camera_summary="$(dumpsys media.camera 2>/dev/null | awk '
    /Number of camera devices:/ && camera_count == "" {
        camera_count = $NF
    }
    /^== Camera device [^ ]+ dynamic info: ==$/ {
        camera_ids = camera_ids camera_separator $4
        camera_separator = ","
    }
    /^[[:space:]]*Device [0-9][0-9]* maps to "/ {
        api1_id = $5
        gsub(/"/, "", api1_id)
        api1_ids = api1_ids api1_separator api1_id
        api1_separator = ","
    }
    END {
        printf "%s\n%s\n%s\n", camera_count, camera_ids, api1_ids
    }
')"
camera_count="$(printf '%s\n' "$camera_summary" | sed -n '1p')"
[ -n "$camera_count" ] || camera_count=unknown
camera_ids="$(printf '%s\n' "$camera_summary" | sed -n '2p')"
[ -n "$camera_ids" ] || camera_ids=unknown
api1_camera_ids="$(printf '%s\n' "$camera_summary" | sed -n '3p')"
[ -n "$api1_camera_ids" ] || api1_camera_ids=none

cameraservice_hash=unknown
if [ -r /system/lib64/libcameraservice.so ]; then
    cameraservice_hash="$(sha256sum /system/lib64/libcameraservice.so 2>/dev/null | awk '{print $1}')"
fi
camera_client_path=none
camera_client_hash=none
for candidate in \
    /system/lib64/libcamera_client.so \
    /system_ext/lib64/libcamera_client.so; do
    [ -r "$candidate" ] || continue
    camera_client_path="$candidate"
    camera_client_hash="$(sha256sum "$candidate" 2>/dev/null | awk '{print $1}')"
    break
done
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

fingerprint="$(prop ro.build.fingerprint)"
profile_id=none
profile_status=unsupported
profile_reason=no_qualified_recipe
case "$fingerprint" in
    nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys)
        profile_id=nx769j-ukq1-20240417
        if [ "$cameraservice_hash" = \
                a26f8ee10002769428871e042c7993e87ad769703897dd75a2fb93a725c64438 ] && \
           [ "$camera_client_hash" = \
                1cf518e86a2e5461e585d8dbd7a1dbc93e7ba2bcc95c3e254ebdcc72ee0433c5 ]; then
            profile_status=qualified
            profile_reason=exact_fingerprint_and_camera_abi
        else
            profile_status=abi_mismatch
            profile_reason=qualified_fingerprint_with_unexpected_camera_abi
        fi
        ;;
    nubia/NX769J/NX769J:14/*)
        profile_id=nx769j-android14-candidate
        profile_status=build_mismatch
        profile_reason=nx769j_android14_build_not_qualified
        ;;
esac

emit_profile() {
    field schema_version 4
    field sdk "$(prop ro.build.version.sdk)"
    field release "$(prop ro.build.version.release)"
    field abi "$(prop ro.product.cpu.abi)"
    field fingerprint "$fingerprint"
    field manufacturer "$(prop ro.product.manufacturer)"
    field product_device "$(prop ro.product.device)"
    field board_platform "$(prop ro.board.platform)"
    field hardware "$(prop ro.hardware)"
    field selinux "$(getenforce 2>/dev/null)"
    field provider_transport "$transport"
    field provider_version "$provider_version"
    field provider_instance "$provider_instance"
    field provider_manifest "$provider_manifest"
    field provider_service "${provider_service:-none}"
    field provider_init_service "$provider_init_service"
    field provider_service_context "$provider_service_context"
    field provider_process_context "$provider_process_context"
    field provider_service_hash "$provider_service_hash"
    field adapter_hint "$adapter_hint"
    field legacy_module "${legacy_module:-none}"
    field legacy_module_hash "$legacy_module_hash"
    field cameraservice_hash "$cameraservice_hash"
    field camera_client_path "$camera_client_path"
    field camera_client_hash "$camera_client_hash"
    field profile_id "$profile_id"
    field profile_status "$profile_status"
    field profile_reason "$profile_reason"
    field route_scope "$([ "$profile_status" = qualified ] && printf per_app || printf unavailable)"
    field virtual_camera_ids "$([ "$profile_status" = qualified ] && printf 1000,1001 || printf none)"
    field camera_count "$camera_count"
    field camera_ids "$camera_ids"
    field api1_camera_ids "$api1_camera_ids"
    field reported_physical_camera_count "$(prop ro.vendor.feature.camera_physical_count)"
    field under_screen_camera "$(prop ro.vendor.feature.camera_under_screen_sensor)"
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
