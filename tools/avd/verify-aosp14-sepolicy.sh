#!/usr/bin/env bash
set -euo pipefail

if (($# < 2 || $# > 3)); then
    echo "usage: verify-aosp14-sepolicy.sh AOSP_ROOT SOURCE_ROOT [JOBS]" >&2
    exit 2
fi

aosp_root=$(cd -- "$1" && pwd)
source_root=$(cd -- "$2" && pwd)
jobs=${3:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')}
build_make="$aosp_root/build/make"
board_patch="$source_root/tools/avd/aosp14-sepolicy-boardconfig.patch"
isolation_patch="$source_root/tools/avd/aosp14-sepolicy-build-isolation.patch"
policy_copy="$aosp_root/vendor/android_vcam_policycheck/sepolicy"
managed_build_copy="$aosp_root/vendor/android_vcam_buildcheck"
hidden_build_copy="$aosp_root/out/android-vcam-r23-sepolicy-managed-copy"

[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || {
    echo "jobs must be a positive integer" >&2
    exit 2
}
[[ -d "$build_make/.git" || -f "$build_make/.git" ]] || {
    echo "AOSP build/make checkout is missing" >&2
    exit 1
}
[[ -f "$board_patch" ]] || {
    echo "AVD BoardConfig patch is missing" >&2
    exit 1
}
[[ -f "$isolation_patch" ]] || {
    echo "AVD policy-build isolation patch is missing" >&2
    exit 1
}
[[ -d "$source_root/aosp/provider/sepolicy" ]] || {
    echo "VCAM product policy is missing" >&2
    exit 1
}
[[ -z "$(git -C "$build_make" status --porcelain)" ]] || {
    echo "AOSP build/make must be pristine" >&2
    exit 1
}

patch_applied=0
isolation_patch_applied=0
build_copy_hidden=0
cleanup() {
    status=$?
    if ((isolation_patch_applied)); then
        git -C "$build_make" apply -R "$isolation_patch" || status=1
    fi
    if ((patch_applied)); then
        git -C "$build_make" apply -R "$board_patch" || status=1
    fi
    if ((build_copy_hidden)); then
        mv "$hidden_build_copy" "$managed_build_copy" || status=1
    fi
    rm -rf -- "$aosp_root/vendor/android_vcam_policycheck" || status=1
    trap - EXIT
    exit "$status"
}
trap cleanup EXIT

mkdir -p "$policy_copy"
rsync -a --delete "$source_root/aosp/provider/sepolicy/" "$policy_copy/"

git -C "$build_make" apply --check "$board_patch"
git -C "$build_make" apply "$board_patch"
patch_applied=1
git -C "$build_make" apply --check "$isolation_patch"
git -C "$build_make" apply "$isolation_patch"
isolation_patch_applied=1

# The regular target validator keeps an Android.bp-bearing managed copy under
# vendor/. It is not part of this policy-only build and would make Soong parse
# Provider modules whose source defaults are patched only by that validator.
if [[ -d "$managed_build_copy" ]]; then
    [[ ! -e "$hidden_build_copy" ]] || {
        echo "refusing to replace existing hidden build copy" >&2
        exit 1
    }
    mv "$managed_build_copy" "$hidden_build_copy"
    build_copy_hidden=1
fi

cd "$aosp_root"
export OUT_DIR="$aosp_root/out/android-vcam-r23-sepolicy-x86_64"
export ALLOW_MISSING_DEPENDENCIES=true
export WITH_DEXPREOPT=false
export DISABLE_PREOPT=true
set +u
source build/envsetup.sh >/dev/null
lunch aosp_x86_64-eng >/dev/null
m -j"$jobs" vendor_sepolicy.cil
set -u

artifact=$(find "$OUT_DIR" -type f -name vendor_sepolicy.cil -print | head -n 1)
[[ -n "$artifact" && -s "$artifact" ]] || {
    echo "vendor_sepolicy.cil was not produced" >&2
    exit 1
}
echo "Verified Android 14 AVD vendor policy: $artifact"
