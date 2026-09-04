#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR=/data/adb/android_vcam_capability_probe
PROFILE_FILE=$STATE_DIR/device-profile.conf
RESULT_FILE=$STATE_DIR/capability-result.conf
TEMP_RESULT=$RESULT_FILE.tmp.$$

read_field() {
    key="$1"
    sed -n "s/^${key}=//p" "$PROFILE_FILE" 2>/dev/null | head -n 1
}

mkdir -p "$STATE_DIR" || exit 1
chmod 0700 "$STATE_DIR"

if ! sh "$MODDIR/device-probe.sh" "$PROFILE_FILE"; then
    echo "Compatibility probe failed" >&2
    exit 1
fi

profile_schema="$(read_field schema_version)"
sdk="$(read_field sdk)"
candidate_status="$(read_field platform_candidate_status)"
candidate_reason="$(read_field platform_candidate_reason)"
recommended_scope="$(read_field recommended_route_scope)"
profile_status="$(read_field profile_status)"
profile_id="$(read_field profile_id)"
transport="$(read_field provider_transport)"
camera_count="$(read_field camera_count)"
camera_ids="$(read_field camera_ids)"
[ -n "$recommended_scope" ] || recommended_scope=unavailable

result_status=blocked
result_reason="$candidate_reason"
if [ "$profile_schema" != "6" ]; then
    result_reason=unexpected_profile_schema
elif [ "$sdk" != "34" ]; then
    result_reason=android_version_not_supported_by_this_probe
elif [ "$candidate_status" = "probe_required" ]; then
    result_status=probe_required
    result_reason=runtime_qualification_required
elif [ "$candidate_status" = "qualified" ]; then
    # An exact recipe may already exist, but this diagnostic package never
    # authorizes or activates it.
    result_status=exact_profile_detected
    result_reason=use_the_supported_release_module_for_activation
fi

official_module_present=false
[ -d /data/adb/modules/android_vcam ] && official_module_present=true

{
    echo "schema_version=1"
    echo "profile_schema_version=$profile_schema"
    echo "generated_at_epoch=$(date +%s 2>/dev/null)"
    echo "result_status=$result_status"
    echo "result_reason=$result_reason"
    echo "platform_candidate_status=$candidate_status"
    echo "platform_candidate_reason=$candidate_reason"
    echo "profile_status=$profile_status"
    echo "profile_id=$profile_id"
    echo "provider_transport=$transport"
    echo "camera_count=$camera_count"
    echo "camera_ids=$camera_ids"
    echo "recommended_route_scope=$recommended_scope"
    echo "activation_policy=probe_only"
    echo "routing_authorized=false"
    echo "camera_mutation_performed=false"
    echo "report_only=true"
    echo "official_module_present=$official_module_present"
} > "$TEMP_RESULT" || { rm -f "$TEMP_RESULT"; exit 1; }

chmod 0600 "$TEMP_RESULT"
mv -f "$TEMP_RESULT" "$RESULT_FILE"
chmod 0600 "$PROFILE_FILE" "$RESULT_FILE"

echo "Compatibility report: $RESULT_FILE"
cat "$RESULT_FILE"
