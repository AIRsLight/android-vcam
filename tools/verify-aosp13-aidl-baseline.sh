#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: verify-aosp13-aidl-baseline.sh --aosp-root PATH [options]

Build the Android 13 upstream Google Camera HAL AIDL service and emulated HWL.
This validates the platform transport baseline; it does not claim that the
android-vcam source and routing adapter have been integrated into that HWL.

Options:
  --jobs N    build parallelism (default: host CPU count)
  -h, --help  show this help
EOF
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

aosp_root=
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
while (($#)); do
    case "$1" in
        --aosp-root)
            (($# >= 2)) || fail "--aosp-root requires a value"
            aosp_root=$2
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
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail "--jobs must be a positive integer"
aosp_root=$(cd -- "$aosp_root" && pwd)
google_camera="$aosp_root/hardware/google/camera"
required_google_camera_commit=4355c55eb23e591e3cdb1f44ca82040f7ddda4a2

for required in \
    "$aosp_root/build/envsetup.sh" \
    "$aosp_root/hardware/interfaces/camera/provider/aidl/Android.bp" \
    "$google_camera/common/hal/aidl_service/Android.bp" \
    "$google_camera/devices/EmulatedCamera/hwl/Android.bp"; do
    [[ -e "$required" ]] || fail "required AOSP AIDL baseline source is missing: $required"
done

head=$(git -C "$google_camera" rev-parse HEAD)
[[ "$head" == "$required_google_camera_commit" ]] ||
    fail "hardware/google/camera must be android-13.0.0_r84 commit $required_google_camera_commit (found $head)"
[[ -z "$(git -C "$google_camera" status --porcelain)" ]] ||
    fail "hardware/google/camera has local changes"

cd "$aosp_root"
export OUT_DIR="$aosp_root/out/android-vcam-r84-soong"
export ALLOW_MISSING_DEPENDENCIES=true
export WITH_DEXPREOPT=false
export DISABLE_PREOPT=true
mkdir -p "$OUT_DIR/soong"
printf 'android-vcam-ci-r84\n' >"$OUT_DIR/soong/build_number.txt"

set +u
source build/envsetup.sh >/dev/null
lunch aosp_arm64-eng >/dev/null
m --soong-only -j"$jobs" \
    13-android.hardware.camera.provider@2.7-service-google \
    14-libgooglecamerahwl_impl
set -u

for artifact in \
    "$OUT_DIR/soong/target/product/generic_arm64/vendor/bin/hw/android.hardware.camera.provider@2.7-service-google" \
    "$OUT_DIR/soong/target/product/generic_arm64/vendor/lib64/libgooglecamerahwl_impl.so"; do
    [[ -s "$artifact" ]] || fail "expected AIDL baseline artifact is missing: $artifact"
    printf 'Verified AIDL baseline artifact: %s\n' "$artifact"
done

printf 'Android 13 upstream AIDL camera baseline built successfully.\n'
