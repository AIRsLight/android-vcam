#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: verify-aosp13-build.sh --aosp-root PATH [options]

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
patch_rel="aosp/cameraservice/android-13/frameworks-av.patch"
patch="$source_root/$patch_rel"
managed_copy="$aosp_root/vendor/android_vcam_buildcheck"
managed_marker="$managed_copy/.vcam-managed-build-copy"
host_compat_source_dir="$aosp_root/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8/sysroot/usr/lib"
required_frameworks_av_commit=95be9bad234d69f4a8ded5ee72b60315b1353098

[[ -f "$aosp_root/build/envsetup.sh" ]] || fail "build/envsetup.sh is missing"
[[ -e "$frameworks_av/.git" ]] || fail "frameworks/av is not a repo checkout"
[[ -f "$patch" ]] || fail "CameraService patch is missing: $patch"

head=$(git -C "$frameworks_av" rev-parse HEAD)
[[ "$head" == "$required_frameworks_av_commit" ]] ||
    fail "frameworks/av must be android-13.0.0_r84 commit $required_frameworks_av_commit (found $head)"

[[ -z "$(git -C "$frameworks_av" status --porcelain)" ]] ||
    fail "frameworks/av has local changes; validation requires a pristine checkout"

git -C "$frameworks_av" apply --check "$patch" ||
    fail "CameraService patch does not apply cleanly to frameworks/av $head"

for required in \
    "$aosp_root/build/soong/soong_ui.bash" \
    "$aosp_root/prebuilts/clang/host/linux-x86" \
    "$aosp_root/prebuilts/jdk/jdk11" \
    "$host_compat_source_dir/libncurses.so.5" \
    "$host_compat_source_dir/libtinfo.so.5" \
    "$aosp_root/system/core"; do
    [[ -e "$required" ]] || fail "required AOSP build dependency is missing: $required"
done

printf 'Android 13 AOSP preflight passed for frameworks/av %s\n' "$head"
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
    trap - EXIT
    if ((original_status != 0)); then
        exit "$original_status"
    fi
    exit "$cleanup_failed"
}
trap cleanup EXIT

git -C "$frameworks_av" apply "$managed_copy/$patch_rel"
patch_applied=1

cd "$aosp_root"
export OUT_DIR="$aosp_root/out/android-vcam-r84-soong"
export ALLOW_MISSING_DEPENDENCIES=true
export WITH_DEXPREOPT=false
export DISABLE_PREOPT=true
host_compat_lib_dir="$OUT_DIR/host-compat-libs"
mkdir -p "$host_compat_lib_dir"
ln -sfn "$host_compat_source_dir/libncurses.so.5.9" "$host_compat_lib_dir/libncurses.so.5"
ln -sfn "$host_compat_source_dir/libtinfo.so.5.9" "$host_compat_lib_dir/libtinfo.so.5"
set +u
source build/envsetup.sh >/dev/null
lunch aosp_arm64-eng >/dev/null
llvm_rs_cc="$OUT_DIR/soong/host/linux-x86/bin/llvm-rs-cc"
m --soong-only -j"$jobs" "$llvm_rs_cc"
if file -b "$llvm_rs_cc" | grep -q '^ELF '; then
    mv -f "$llvm_rs_cc" "$llvm_rs_cc.real"
fi
[[ -x "$llvm_rs_cc.real" ]] || fail "real llvm-rs-cc host tool is missing"
install -m 0755 \
    "$managed_copy/tools/host-wrappers/llvm-rs-cc" \
    "$llvm_rs_cc"
m --soong-only -j"$jobs" WITH_DEXPREOPT=false \
    libcameraservice \
    camera.vcam \
    android.hardware.camera.provider@2.4-vcam-service
set -u

for artifact in \
    "$OUT_DIR/soong/target/product/generic_arm64/system/lib64/libcameraservice.so" \
    "$OUT_DIR/soong/target/product/generic_arm64/vendor/lib64/hw/camera.vcam.so" \
    "$OUT_DIR/soong/target/product/generic_arm64/vendor/bin/hw/android.hardware.camera.provider@2.4-vcam-service"; do
    [[ -s "$artifact" ]] || fail "expected build artifact is missing: $artifact"
    printf 'Verified artifact: %s\n' "$artifact"
done

printf 'Android 13 AOSP targets built successfully in %s\n' "$OUT_DIR"
