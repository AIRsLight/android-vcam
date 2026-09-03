#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: verify-aosp14-build.sh --aosp-root PATH [options]

Options:
  --source-root PATH  android_vcam repository root (defaults to script parent)
  --mode MODE         check or build (default: check)
  --product PRODUCT   aosp_arm64 or aosp_x86_64 (default: aosp_arm64)
  --jobs N            build parallelism (default: host CPU count)
  -h, --help          show this help
EOF
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_root=$(cd -- "$script_dir/.." && pwd)
aosp_root=
mode=check
product=aosp_arm64
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')

while (($#)); do
    case "$1" in
        --aosp-root)
            (($# >= 2)) || fail "--aosp-root requires a value"
            aosp_root=$2
            shift 2
            ;;
        --source-root)
            (($# >= 2)) || fail "--source-root requires a value"
            source_root=$2
            shift 2
            ;;
        --mode)
            (($# >= 2)) || fail "--mode requires a value"
            mode=$2
            shift 2
            ;;
        --product)
            (($# >= 2)) || fail "--product requires a value"
            product=$2
            shift 2
            ;;
        --jobs)
            (($# >= 2)) || fail "--jobs requires a value"
            jobs=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown argument: $1"
            ;;
    esac
done

[[ -n "$aosp_root" ]] || fail "--aosp-root is required"
[[ "$mode" == check || "$mode" == build ]] || fail "--mode must be check or build"
[[ "$product" == aosp_arm64 || "$product" == aosp_x86_64 ]] ||
    fail "--product must be aosp_arm64 or aosp_x86_64"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail "--jobs must be a positive integer"

if [[ "$product" == aosp_arm64 ]]; then
    product_output=generic_arm64
    output_name=android-vcam-r23-soong
else
    product_output=generic_x86_64
    output_name=android-vcam-r23-soong-x86_64
fi

aosp_root=$(cd -- "$aosp_root" && pwd)
source_root=$(cd -- "$source_root" && pwd)
frameworks_av="$aosp_root/frameworks/av"
patch_rel="aosp/cameraservice/android-14/frameworks-av.patch"
patch="$source_root/$patch_rel"
boundary_patch_rel="aosp/cameraservice/android-14/frameworks-av-boundary.patch"
boundary_patch="$source_root/$boundary_patch_rel"
discovery_patch_rel="aosp/cameraservice/android-14/frameworks-av-provider-discovery.patch"
discovery_patch="$source_root/$discovery_patch_rel"
security_patch_rel="aosp/cameraservice/android-14/frameworks-av-provider-security.patch"
security_patch="$source_root/$security_patch_rel"
google_camera="$aosp_root/hardware/google/camera"
google_patch_rel="aosp/provider/aidl/android-14/hardware-google-camera.patch"
google_patch="$source_root/$google_patch_rel"
managed_copy="$aosp_root/vendor/android_vcam_buildcheck"
managed_marker="$managed_copy/.vcam-managed-build-copy"
required_frameworks_av_commit=ba08f2dd8ffc3a94c3bbdebb3f9109f78bd09f93
required_google_camera_commit=11f9bcc895240629b4cd88a6a595a9ef326490ff

[[ -f "$aosp_root/build/envsetup.sh" ]] || fail "build/envsetup.sh is missing"
[[ -e "$frameworks_av/.git" ]] || fail "frameworks/av is not a repo checkout"
[[ -e "$google_camera/.git" ]] || fail "hardware/google/camera is not a repo checkout"
[[ -f "$patch" ]] || fail "CameraService patch is missing: $patch"
[[ -f "$boundary_patch" ]] || fail "CameraService boundary patch is missing: $boundary_patch"
[[ -f "$discovery_patch" ]] || fail "CameraService provider discovery patch is missing: $discovery_patch"
[[ -f "$security_patch" ]] || fail "CameraService provider security patch is missing: $security_patch"
[[ -f "$google_patch" ]] || fail "Google Camera patch is missing: $google_patch"

head=$(git -C "$frameworks_av" rev-parse HEAD)
[[ "$head" == "$required_frameworks_av_commit" ]] ||
    fail "frameworks/av must be android-14.0.0_r23 commit $required_frameworks_av_commit (found $head)"

[[ -z "$(git -C "$frameworks_av" status --porcelain)" ]] ||
    fail "frameworks/av has local changes; validation requires a pristine checkout"
google_head=$(git -C "$google_camera" rev-parse HEAD)
[[ "$google_head" == "$required_google_camera_commit" ]] ||
    fail "hardware/google/camera must be android-14.0.0_r23 commit $required_google_camera_commit (found $google_head)"
[[ -z "$(git -C "$google_camera" status --porcelain)" ]] ||
    fail "hardware/google/camera has local changes; validation requires a pristine checkout"

git -C "$frameworks_av" apply --check "$patch" ||
    fail "CameraService patch does not apply cleanly to frameworks/av $head"
git -C "$frameworks_av" apply --check "$discovery_patch" ||
    fail "CameraService provider discovery patch does not apply cleanly to frameworks/av $head"
git -C "$frameworks_av" apply --check "$security_patch" ||
    fail "CameraService provider security patch does not apply cleanly to frameworks/av $head"
git -C "$frameworks_av" apply "$patch"
if ! git -C "$frameworks_av" apply --check "$boundary_patch"; then
    git -C "$frameworks_av" apply -R "$patch" || true
    fail "CameraService boundary patch does not apply after the routing patch"
fi
git -C "$frameworks_av" apply -R "$patch" ||
    fail "unable to restore frameworks/av after boundary patch preflight"
[[ -z "$(git -C "$frameworks_av" status --porcelain)" ]] ||
    fail "frameworks/av is not pristine after CameraService patch preflight"
git -C "$google_camera" apply --check "$google_patch" ||
    fail "AIDL transport patch does not apply cleanly to hardware/google/camera $google_head"
bash "$source_root/tests/test-camera-map.sh" ||
    fail "portable camera map tests failed"

for required in \
    "$aosp_root/build/soong/soong_ui.bash" \
    "$aosp_root/prebuilts/bazel/common" \
    "$aosp_root/prebuilts/bazel/linux-x86_64" \
    "$aosp_root/prebuilts/build-tools/linux-x86" \
    "$aosp_root/prebuilts/clang/host/linux-x86" \
    "$aosp_root/prebuilts/go/linux-x86" \
    "$aosp_root/prebuilts/jdk/jdk11" \
    "$aosp_root/prebuilts/jdk/jdk17" \
    "$aosp_root/frameworks/base" \
    "$aosp_root/frameworks/native" \
    "$aosp_root/hardware/libhardware" \
    "$aosp_root/system/core"; do
    [[ -e "$required" ]] || fail "required AOSP build dependency is missing: $required"
done

printf 'Android 14 AOSP preflight passed for frameworks/av %s\n' "$head"
if [[ "$mode" == check ]]; then
    printf 'No files were changed. Re-run with --mode build for a Soong compile.\n'
    exit 0
fi

command -v rsync >/dev/null 2>&1 || fail "rsync is required"
if [[ -e "$managed_copy" && ! -f "$managed_marker" ]]; then
    fail "refusing to overwrite unmarked directory: $managed_copy"
fi

mkdir -p "$managed_copy"
rsync -a --delete \
    --exclude '/.git/' \
    --exclude '/.reference/' \
    --exclude '/out/' \
    --exclude '/dist/' \
    --exclude '/.vcam-managed-build-copy' \
    "$source_root/" "$managed_copy/"
touch "$managed_marker"

patch_applied=0
boundary_patch_applied=0
google_patch_applied=0
discovery_patch_applied=0
security_patch_applied=0
cleanup() {
    original_status=$?
    set +e
    cleanup_failed=0
    if ((security_patch_applied)); then
        if git -C "$frameworks_av" apply -R --check "$managed_copy/$security_patch_rel" &&
                git -C "$frameworks_av" apply -R "$managed_copy/$security_patch_rel"; then
            printf 'Removed temporary CameraService provider security patch.\n'
        else
            printf 'ERROR: unable to roll back the provider security patch\n' >&2
            cleanup_failed=1
        fi
    fi
    if ((discovery_patch_applied)); then
        if git -C "$frameworks_av" apply -R --check "$managed_copy/$discovery_patch_rel" &&
                git -C "$frameworks_av" apply -R "$managed_copy/$discovery_patch_rel"; then
            printf 'Removed temporary CameraService provider discovery patch.\n'
        else
            printf 'ERROR: unable to roll back the provider discovery patch\n' >&2
            cleanup_failed=1
        fi
    fi
    if ((boundary_patch_applied)); then
        if git -C "$frameworks_av" apply -R --check "$managed_copy/$boundary_patch_rel" &&
                git -C "$frameworks_av" apply -R "$managed_copy/$boundary_patch_rel"; then
            printf 'Removed temporary CameraService boundary patch.\n'
        else
            printf 'ERROR: unable to roll back the CameraService boundary patch\n' >&2
            cleanup_failed=1
        fi
    fi
    if ((patch_applied)); then
        if git -C "$frameworks_av" apply -R --check "$managed_copy/$patch_rel" &&
                git -C "$frameworks_av" apply -R "$managed_copy/$patch_rel"; then
            printf 'Restored pristine frameworks/av checkout.\n'
        else
            printf 'ERROR: unable to roll back the temporary CameraService patch\n' >&2
            cleanup_failed=1
        fi
    fi
    if ((google_patch_applied)); then
        if git -C "$google_camera" apply -R --check "$managed_copy/$google_patch_rel" &&
                git -C "$google_camera" apply -R "$managed_copy/$google_patch_rel"; then
            printf 'Restored pristine hardware/google/camera checkout.\n'
        else
            printf 'ERROR: unable to roll back the temporary Google Camera patch\n' >&2
            cleanup_failed=1
        fi
    fi
    trap - EXIT
    if ((original_status != 0)); then
        exit "$original_status"
    fi
    exit "$cleanup_failed"
}
trap cleanup EXIT

git -C "$frameworks_av" apply "$managed_copy/$patch_rel"
patch_applied=1
git -C "$frameworks_av" apply "$managed_copy/$boundary_patch_rel"
boundary_patch_applied=1
git -C "$frameworks_av" apply "$managed_copy/$discovery_patch_rel"
discovery_patch_applied=1
git -C "$frameworks_av" apply "$managed_copy/$security_patch_rel"
security_patch_applied=1
git -C "$google_camera" apply "$managed_copy/$google_patch_rel"
google_patch_applied=1

cd "$aosp_root"
export OUT_DIR="$aosp_root/out/$output_name"
export ALLOW_MISSING_DEPENDENCIES=true
export WITH_DEXPREOPT=false
export DISABLE_PREOPT=true
set +u
source build/envsetup.sh >/dev/null
lunch "${product}-eng" >/dev/null
# --soong-only skips Kati, while Google's AIDL camera version generator reads
# the standard build number file that Kati normally creates. Query AOSP's own
# dumpvar interface, then mirror main.mk's compare-and-replace behavior without
# evaluating unrelated product installation rules in this reduced checkout.
build_number=$(build/soong/soong_ui.bash --dumpvar-mode BUILD_NUMBER)
[[ -n "$build_number" && "$build_number" != *$'\n'* ]] ||
    fail "AOSP dumpvar returned an invalid BUILD_NUMBER"
mkdir -p "$OUT_DIR/soong"
printf '%s' "$build_number" >"$OUT_DIR/soong/build_number.tmp"
if ! cmp -s "$OUT_DIR/soong/build_number.tmp" "$OUT_DIR/soong/build_number.txt"; then
    mv "$OUT_DIR/soong/build_number.tmp" "$OUT_DIR/soong/build_number.txt"
else
    rm "$OUT_DIR/soong/build_number.tmp"
fi
[[ -s "$OUT_DIR/soong/build_number.txt" ]] ||
    fail "AOSP build initialization did not create soong/build_number.txt"
m --soong-only -j"$jobs" WITH_DEXPREOPT=false \
    libcameraservice \
    camera.vcam \
    android.hardware.camera.provider@2.4-vcam-service \
    libvcam_googlecamerahwl_impl \
    android.hardware.camera.provider-service-vcam-v2 \
    vcam_provider_probe_client \
    libvcam_cameraserver_router \
    vcam_cameraserver_launcher \
    vcam_android14_camera_service_profile_test \
    vcam_android14_protocol_evidence_test \
    vcam_android14_parcel_observer_test
set -u

for artifact in \
    "$OUT_DIR/soong/target/product/$product_output/system/lib64/libcameraservice.so" \
    "$OUT_DIR/soong/target/product/$product_output/vendor/lib64/hw/camera.vcam.so" \
    "$OUT_DIR/soong/target/product/$product_output/vendor/bin/hw/android.hardware.camera.provider@2.4-vcam-service" \
    "$OUT_DIR/soong/target/product/$product_output/system/bin/vcam_provider_probe_client" \
    "$OUT_DIR/soong/target/product/$product_output/system/bin/vcam_cameraserver_launcher" \
    "$OUT_DIR/soong/target/product/$product_output/system/lib64/libvcam_cameraserver_router.so" \
    "$OUT_DIR/soong/target/product/$product_output/vendor/lib64/libvcam_googlecamerahwl_impl.so" \
    "$OUT_DIR/soong/target/product/$product_output/vendor/bin/hw/android.hardware.camera.provider-service-vcam-v2"; do
    [[ -s "$artifact" ]] || fail "expected build artifact is missing: $artifact"
    printf 'Verified artifact: %s\n' "$artifact"
done

for test_name in \
    vcam_android14_camera_service_profile_test \
    vcam_android14_protocol_evidence_test \
    vcam_android14_parcel_observer_test; do
    test_binary=$(find "$OUT_DIR/soong/host/linux-x86" -type f -name "$test_name" \
        -perm /111 -print | head -n 1)
    [[ -n "$test_binary" ]] || fail "host test binary is missing: $test_name"
    "$test_binary" || fail "host test failed: $test_name"
    printf 'Verified host test: %s\n' "$test_name"
done

hwl_artifact="$OUT_DIR/soong/target/product/$product_output/vendor/lib64/libvcam_googlecamerahwl_impl.so"
llvm_nm=$(find "$aosp_root/prebuilts/clang/host/linux-x86" \
    -path '*/bin/llvm-nm' -type f -print | sort -V | tail -n 1)
[[ -x "$llvm_nm" ]] || fail "llvm-nm is missing from the Android 14 prebuilts"

# The systemless launcher recreates main_cameraserver in its already-transitioned
# process. Refuse a platform build whose libcameraservice does not export the
# single private entry point required by that bootstrap contract.
cameraservice_artifact="$OUT_DIR/soong/target/product/$product_output/system/lib64/libcameraservice.so"
cameraservice_symbols=$("$llvm_nm" -D --defined-only "$cameraservice_artifact")
grep -Fq '_ZN7android13CameraService11instantiateEv' <<<"$cameraservice_symbols" || \
    fail "libcameraservice does not export CameraService::instantiate()"
printf 'Verified launcher entry point: CameraService::instantiate()\n'

hwl_symbols=$("$llvm_nm" -D --defined-only "$hwl_artifact")
for symbol in \
    CreateCameraProviderHwl \
    VcamAdjustCameraMetadata \
    VcamSetActiveFrame \
    VcamRenderYuv420 \
    VcamRenderRgb; do
    grep -q " $symbol$" <<<"$hwl_symbols" ||
        fail "AIDL HWL frame hook is missing from the linked artifact: $symbol"
    printf 'Verified AIDL HWL symbol: %s\n' "$symbol"
done

printf 'Android 14 AOSP %s targets built successfully in %s\n' "$product" "$OUT_DIR"
