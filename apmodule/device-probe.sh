#!/system/bin/sh

# Produces a transport-neutral device profile used to select an AOSP or OEM
# camera adapter. Values are deliberately line-oriented so both shell code and
# the root-free manager can consume them without a JSON parser.

OUTPUT="$1"
TEMPORARY=""
SCRIPT_DIR=${0%/*}

clean_value() {
    printf '%s' "$1" | tr '\r\n\t' '   '
}

field() {
    printf '%s=%s\n' "$1" "$(clean_value "$2")"
}

prop() {
    getprop "$1" 2>/dev/null
}

run_bounded() {
    seconds="$1"
    shift
    if command -v timeout >/dev/null 2>&1; then
        timeout "$seconds" "$@"
    else
        "$@"
    fi
}

# Some OEM and AVD service managers can stall while enumerating a later Binder
# entry. Preserve any early output, but never let a read-only profile refresh
# hold a module service or Manager request indefinitely.
camera_lshal="$(run_bounded 5 lshal 2>/dev/null | grep 'android.hardware.camera.provider@' || true)"
camera_service_check="$(run_bounded 3 service check media.camera 2>/dev/null || true)"
camera_service_binder=0
case "$camera_service_check" in
    *': found'*) camera_service_binder=1 ;;
esac
aidl_service_line="$(run_bounded 5 service list 2>/dev/null | sed -n \
    '/android\.hardware\.camera\.provider\.ICameraProvider\//{p;q;}' || true)"

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

camera_summary="$(run_bounded 8 dumpsys media.camera 2>/dev/null | awk '
    /Number of camera devices:/ && camera_count == "" {
        camera_count = $NF
    }
    /^== Camera device [^ ]+ dynamic info: ==$/ {
        camera_id = $4
        if (!(camera_id in seen_camera_ids)) {
            camera_ids = camera_ids camera_separator camera_id
            camera_separator = ","
            seen_camera_ids[camera_id] = 1
        }
    }
    /^[[:space:]]*Device [0-9][0-9]* maps to "/ {
        api1_id = $5
        gsub(/"/, "", api1_id)
        if (!(api1_id in seen_api1_ids)) {
            api1_ids = api1_ids api1_separator api1_id
            api1_separator = ","
            seen_api1_ids[api1_id] = 1
        }
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
proxy_slot_path=/vendor/lib64/hw/local_time.default.so
proxy_slot_hash=none
if [ -r "$proxy_slot_path" ]; then
    proxy_slot_hash="$(sha256sum "$proxy_slot_path" 2>/dev/null | awk '{print $1}')"
fi
legacy_module_hash=none
if [ -n "$legacy_module" ]; then
    legacy_module_hash="$(sha256sum "$legacy_module" 2>/dev/null | awk '{print $1}')"
fi
oneplus_hal_path=/vendor/lib64/hw/camera.qcom.so
oneplus_hal_hash=none
if [ -r "$oneplus_hal_path" ]; then
    oneplus_hal_hash="$(sha256sum "$oneplus_hal_path" 2>/dev/null | awk '{print $1}')"
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
sdk="$(prop ro.build.version.sdk)"
abi="$(prop ro.product.cpu.abi)"
selinux_state="$(getenforce 2>/dev/null)"
profile_id=none
profile_status=unsupported
profile_reason=no_qualified_recipe
profile_adapter=none
profile_route_scope=unavailable
profile_virtual_camera_ids=none

hash_matches_file() {
    actual="$1"
    candidate="$2"
    [ -r "$candidate" ] || return 1
    [ "$actual" = "$(sha256sum "$candidate" 2>/dev/null | awk '{print $1}')" ]
}

oneplus_payload_matches=false
if hash_matches_file "$oneplus_hal_hash" "$SCRIPT_DIR/vendor/lib64/hw/camera.qcom.so" &&
   hash_matches_file "$proxy_slot_hash" "$SCRIPT_DIR/vendor/lib64/libvcam_proxy.so" &&
   hash_matches_file "$cameraservice_hash" "$SCRIPT_DIR/system/lib64/libcameraservice.so"; then
    oneplus_payload_matches=true
fi

case "$fingerprint" in
    OnePlus/OnePlus7Pro_CH/OnePlus7Pro:12/SKQ1.211113.001/P.202303230244:user/release-keys)
        profile_id=oneplus7pro-p202303230244
        profile_adapter=oneplus7pro-oem-hal
        profile_route_scope=per_app
        if { { [ "$oneplus_hal_hash" = \
                    dab50dfd0bde9f710c92097442d6451695f7ef82cb9e836b0af2b9369751daa6 ] ||
                [ "$oneplus_hal_hash" = \
                    66d5f38e8a6f5a287a661a06e1224fef477bb41574ca61f7091b5682b9b587d5 ]; } &&
              [ "$proxy_slot_hash" = \
                    6ac900f7c1b17fb5551a673ded1fc11469c53dac329bcbbb17b97dd57d2cc992 ] &&
              [ "$cameraservice_hash" = \
                    2108be5d63b385282d844f689e9f34740026072b8ef6daca2ed59b23612870af ]; } ||
           [ "$oneplus_payload_matches" = true ]; then
            profile_status=qualified
            profile_reason=exact_fingerprint_and_oem_camera_abi
        else
            profile_status=abi_mismatch
            profile_reason=qualified_fingerprint_with_unexpected_camera_abi
        fi
        ;;
    OnePlus/OnePlus7Pro_CH/OnePlus7Pro:12/*)
        profile_id=oneplus7pro-android12-candidate
        profile_status=build_mismatch
        profile_reason=oneplus7pro_android12_build_not_qualified
        profile_adapter=oneplus7pro-oem-hal
        ;;
    nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys)
        profile_id=nx769j-ukq1-20240417
        profile_adapter=nx769j-aidl-router
        profile_route_scope=per_app
        profile_virtual_camera_ids=1000,1001
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
        profile_adapter=nx769j-aidl-router
        ;;
esac

# A platform candidate is diagnostic only. It describes whether the common
# Android 14 qualification sequence is meaningful on this build; it never
# promotes an unknown fingerprint into a routing-authorized recipe.
platform_family=unsupported
platform_candidate_status=blocked
platform_candidate_reason=android_version_not_supported_by_this_probe
recommended_route_scope=unavailable
activation_policy=blocked
routing_authorized=false
qualification_basis=none
candidate_requirements=none

if [ "$profile_status" = qualified ]; then
    platform_family=exact-device-profile
    platform_candidate_status=qualified
    platform_candidate_reason=exact_profile_and_camera_abi_verified
    recommended_route_scope="$profile_route_scope"
    activation_policy=exact_profile
    routing_authorized=true
    qualification_basis=committed_recipe
elif [ "$sdk" = 34 ]; then
    platform_family=android14-camera-service
    recommended_route_scope=global_only
    activation_policy=probe_only
    qualification_basis=runtime_probe_required
    candidate_requirements=enforcing_provider_registration,pass_through_protocol,topology_maps,global_preview,reboot_recovery
    case "$abi" in
        arm64-v8a|x86_64)
            if [ "$camera_service_binder" = 0 ]; then
                platform_candidate_reason=media_camera_service_unavailable
            elif [ "$transport" = unknown ]; then
                platform_candidate_reason=camera_provider_transport_unresolved
            elif [ "$selinux_state" != Enforcing ]; then
                platform_candidate_reason=selinux_enforcing_required
            else
                platform_candidate_status=probe_required
                platform_candidate_reason=requires_non_authorizing_runtime_qualification
            fi
            ;;
        *)
            platform_candidate_reason=unsupported_runtime_abi
            ;;
    esac
fi

emit_profile() {
    field schema_version 6
    field sdk "$sdk"
    field release "$(prop ro.build.version.release)"
    field abi "$abi"
    field fingerprint "$fingerprint"
    field manufacturer "$(prop ro.product.manufacturer)"
    field product_device "$(prop ro.product.device)"
    field board_platform "$(prop ro.board.platform)"
    field hardware "$(prop ro.hardware)"
    field selinux "$selinux_state"
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
    field profile_camera_module_path "$([ "$profile_adapter" = oneplus7pro-oem-hal ] && printf '%s' "$oneplus_hal_path" || printf '%s' "${legacy_module:-none}")"
    field profile_camera_module_hash "$([ "$profile_adapter" = oneplus7pro-oem-hal ] && printf '%s' "$oneplus_hal_hash" || printf '%s' "$legacy_module_hash")"
    field proxy_slot_path "$proxy_slot_path"
    field proxy_slot_hash "$proxy_slot_hash"
    field cameraservice_hash "$cameraservice_hash"
    field camera_client_path "$camera_client_path"
    field camera_client_hash "$camera_client_hash"
    field profile_id "$profile_id"
    field profile_status "$profile_status"
    field profile_reason "$profile_reason"
    field profile_adapter "$profile_adapter"
    field route_scope "$([ "$profile_status" = qualified ] && printf '%s' "$profile_route_scope" || printf unavailable)"
    field virtual_camera_ids "$([ "$profile_status" = qualified ] && printf '%s' "$profile_virtual_camera_ids" || printf none)"
    field platform_family "$platform_family"
    field platform_candidate_status "$platform_candidate_status"
    field platform_candidate_reason "$platform_candidate_reason"
    field recommended_route_scope "$recommended_route_scope"
    field activation_policy "$activation_policy"
    field routing_authorized "$routing_authorized"
    field qualification_basis "$qualification_basis"
    field candidate_requirements "$candidate_requirements"
    field camera_count "$camera_count"
    field camera_ids "$camera_ids"
    field api1_camera_ids "$api1_camera_ids"
    field reported_physical_camera_count "$(prop ro.vendor.feature.camera_physical_count)"
    field under_screen_camera "$(prop ro.vendor.feature.camera_under_screen_sensor)"
    field camera_service_binder "$camera_service_binder"
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
