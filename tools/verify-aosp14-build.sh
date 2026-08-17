#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: verify-aosp14-build.sh --aosp-root PATH [options]

Options:
  --source-root PATH  android_vcam repository root (defaults to script parent)
  --mode MODE         check or build (default: check)
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
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail "--jobs must be a positive integer"

aosp_root=$(cd -- "$aosp_root" && pwd)
source_root=$(cd -- "$source_root" && pwd)
frameworks_av="$aosp_root/frameworks/av"
patch_rel="aosp/cameraservice/android-14/frameworks-av.patch"
patch="$source_root/$patch_rel"
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
git -C "$google_camera" apply --check "$google_patch" ||
    fail "AIDL transport patch does not apply cleanly to hardware/google/camera $google_head"

for required in \
    "$aosp_root/build/soong/soong_ui.bash" \
    "$aosp_root/prebuilts/clang/host/linux-x86" \
    "$aosp_root/prebuilts/jdk/jdk17" \
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
google_patch_applied=0
cleanup() {
    original_status=$?
    set +e
    cleanup_failed=0
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
git -C "$google_camera" apply "$managed_copy/$google_patch_rel"
google_patch_applied=1

cd "$aosp_root"
export OUT_DIR="$aosp_root/out/android-vcam-r23-soong"
export ALLOW_MISSING_DEPENDENCIES=true
export WITH_DEXPREOPT=false
export DISABLE_PREOPT=true
set +u
source build/envsetup.sh >/dev/null
lunch aosp_arm64-eng >/dev/null
m --soong-only -j"$jobs" WITH_DEXPREOPT=false \
    libcameraservice \
    libvcam_googlecamerahwl_impl \
    android.hardware.camera.provider-service-vcam-v2
set -u

for artifact in \
    "$OUT_DIR/soong/target/product/generic_arm64/system/lib64/libcameraservice.so" \
    "$OUT_DIR/soong/target/product/generic_arm64/vendor/lib64/libvcam_googlecamerahwl_impl.so" \
    "$OUT_DIR/soong/target/product/generic_arm64/vendor/bin/hw/android.hardware.camera.provider-service-vcam"; do
    [[ -s "$artifact" ]] || fail "expected build artifact is missing: $artifact"
    printf 'Verified artifact: %s\n' "$artifact"
done

hwl_artifact="$OUT_DIR/soong/target/product/generic_arm64/vendor/lib64/libvcam_googlecamerahwl_impl.so"
llvm_readelf=$(find "$aosp_root/prebuilts/clang/host/linux-x86" \
    -path '*/bin/llvm-readelf' -type f -print -quit)
[[ -x "$llvm_readelf" ]] || fail "llvm-readelf is missing from the Android 14 prebuilts"
hwl_symbols=$("$llvm_readelf" -Ws "$hwl_artifact")
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

printf 'Android 14 AOSP targets built successfully in %s\n' "$OUT_DIR"
