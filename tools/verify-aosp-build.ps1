[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$AospRoot,

    [string]$WslDistro = "Ubuntu-26.04",

    [ValidateSet("Check", "Build")]
    [string]$Mode = "Check",

    [ValidateRange(1, 128)]
    [int]$Jobs = [Environment]::ProcessorCount
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

function ConvertTo-BashLiteral {
    param([Parameter(Mandatory = $true)][string]$Value)
    return "'" + $Value.Replace("'", "'\''") + "'"
}

function Invoke-WslBash {
    param([Parameter(Mandatory = $true)][string]$Script)

    $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Script))
    & wsl.exe -d $WslDistro -- bash -lc "echo '$encoded' | base64 -d | bash"
    if ($LASTEXITCODE -ne 0) {
        throw "WSL AOSP validation failed with exit code $LASTEXITCODE"
    }
}

$pathScript = "wslpath -a -- " + (ConvertTo-BashLiteral $repoRoot)
$pathEncoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($pathScript))
$linuxRepoRoot = (& wsl.exe -d $WslDistro -- bash -lc "echo '$pathEncoded' | base64 -d | bash").Trim()
if ($LASTEXITCODE -ne 0 -or -not $linuxRepoRoot) {
    throw "Unable to translate repository path into WSL: $repoRoot"
}

$script = @'
set -e -o pipefail

aosp_root=__AOSP_ROOT__
source_root=__SOURCE_ROOT__
mode=__MODE__
jobs=__JOBS__
patch_rel="aosp/cameraservice/android-12/frameworks-av.patch"
patch="$source_root/$patch_rel"
frameworks_av="$aosp_root/frameworks/av"
frameworks_base="$aosp_root/frameworks/base"
frameworks_base_bp="$frameworks_base/Android.bp"
frameworks_base_bp_hidden="$frameworks_base/.vcam-buildcheck-Android.bp"
managed_copy="$aosp_root/vendor/android_vcam_buildcheck"
managed_marker="$managed_copy/.vcam-managed-build-copy"
frameworks_base_license_bp="$managed_copy/aosp/cameraservice/android-12/frameworks-base-license.bp"
required_frameworks_av_commit="28c005633b2b3867d403ee0ceb8fded4b319e3ad"

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

[ -d "$aosp_root" ] || fail "AOSP root does not exist: $aosp_root"
[ -f "$aosp_root/build/envsetup.sh" ] || fail "build/envsetup.sh is missing"
[ -e "$frameworks_av/.git" ] || fail "frameworks/av is not a repo checkout"
[ -f "$patch" ] || fail "CameraService patch is missing: $patch"

head=$(git -C "$frameworks_av" rev-parse HEAD)
[ "$head" = "$required_frameworks_av_commit" ] ||
    fail "frameworks/av must be android-12.0.0_r34 commit $required_frameworks_av_commit (found $head)"

if [ -n "$(git -C "$frameworks_av" status --porcelain)" ]; then
    fail "frameworks/av has local changes; validation will not modify a dirty checkout"
fi

git -C "$frameworks_av" apply --check "$patch" ||
    fail "CameraService patch does not apply cleanly to frameworks/av $head"

for required in \
    "$aosp_root/build/soong/soong_ui.bash" \
    "$aosp_root/prebuilts/clang/host/linux-x86" \
    "$aosp_root/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8/sysroot/usr/lib/libncurses.so.5" \
    "$aosp_root/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8/sysroot/usr/lib/libtinfo.so.5" \
    "$aosp_root/prebuilts/jdk/jdk11" \
    "$aosp_root/system/core"; do
    [ -e "$required" ] || fail "required AOSP build dependency is missing: $required"
done

printf 'AOSP preflight passed for frameworks/av %s\n' "$head"
if [ "$mode" = Check ]; then
    printf 'No files were changed. Re-run with -Mode Build for a Soong compile.\n'
    exit 0
fi

command -v rsync >/dev/null 2>&1 || fail "rsync is required inside WSL"
if [ -e "$managed_copy" ] && [ ! -f "$managed_marker" ]; then
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
base_bp_hidden=0
prune_markers=()
cleanup() {
    original_status=$?
    set +e
    cleanup_failed=0
    if [ "${#prune_markers[@]}" -gt 0 ]; then
        rm -f -- "${prune_markers[@]}" || cleanup_failed=1
    fi
    if [ "$base_bp_hidden" -eq 1 ] && [ -f "$frameworks_base_bp_hidden" ]; then
        if ! rm -f -- "$frameworks_base_bp" ||
                ! mv -- "$frameworks_base_bp_hidden" "$frameworks_base_bp"; then
            printf 'ERROR: unable to restore frameworks/base/Android.bp\n' >&2
            cleanup_failed=1
        fi
    fi
    if [ "$patch_applied" -eq 1 ]; then
        if git -C "$frameworks_av" apply -R --check "$managed_copy/$patch_rel"; then
            if git -C "$frameworks_av" apply -R "$managed_copy/$patch_rel"; then
                printf 'Restored pristine frameworks/av checkout.\n'
            else
                printf 'ERROR: unable to roll back the temporary CameraService patch\n' >&2
                cleanup_failed=1
            fi
        else
            printf 'ERROR: unable to roll back the temporary CameraService patch\n' >&2
            cleanup_failed=1
        fi
    fi
    trap - EXIT
    if [ "$original_status" -ne 0 ]; then
        exit "$original_status"
    fi
    exit "$cleanup_failed"
}
trap cleanup EXIT

git -C "$frameworks_av" apply "$managed_copy/$patch_rel"
patch_applied=1

# This validator is intentionally usable with a partial AOSP checkout. Hide
# unrelated Java application/test trees from Soong's global module scan; their
# incomplete dexpreopt graphs can panic Android 12 Soong even though none of
# them are dependencies of the native camera targets below.
add_prune_marker() {
    prune_dir=$1
    [ -d "$prune_dir" ] || return 0
    prune_marker="$prune_dir/.find-ignore"
    if [ ! -e "$prune_marker" ]; then
        touch "$prune_marker"
        prune_markers+=("$prune_marker")
    fi
}

for prune_dir in \
    "$aosp_root/bootable/recovery/tools" \
    "$aosp_root/bootable/recovery/updater_sample" \
    "$aosp_root/cts" \
    "$aosp_root/development" \
    "$aosp_root/developers" \
    "$aosp_root/device/google/atv" \
    "$aosp_root/device/google/cuttlefish" \
    "$aosp_root/device/generic/goldfish/MultiDisplayProvider" \
    "$aosp_root/external/angle" \
    "$aosp_root/external/chromium-webview" \
    "$aosp_root/external/ims" \
    "$aosp_root/external/libtextclassifier" \
    "$aosp_root/frameworks/ex" \
    "$aosp_root/frameworks/opt" \
    "$aosp_root/frameworks/native/opengl/tests" \
    "$aosp_root/system/linkerconfig/testmodules"; do
    add_prune_marker "$prune_dir"
done
add_prune_marker "$frameworks_av/apex"

# assemble_vintf links a small VTS helper even for a product build. Keep only
# that helper's path and hide the rest of the very large test module graph.
for child in "$aosp_root/test"/*; do
    [ -d "$child" ] || continue
    [ "$(basename "$child")" = vts-testcase ] || add_prune_marker "$child"
done
for child in "$aosp_root/test/vts-testcase"/*; do
    [ -d "$child" ] || continue
    [ "$(basename "$child")" = hal ] || add_prune_marker "$child"
done
for child in "$aosp_root/test/vts-testcase/hal"/*; do
    [ -d "$child" ] || continue
    [ "$(basename "$child")" = treble ] || add_prune_marker "$child"
done
for child in "$aosp_root/test/vts-testcase/hal/treble"/*; do
    [ -d "$child" ] || continue
    [ "$(basename "$child")" = vintf ] || add_prune_marker "$child"
done

for child in "$aosp_root/packages"/*; do
    [ -d "$child" ] || continue
    [ "$(basename "$child")" = modules ] || add_prune_marker "$child"
done
for child in "$aosp_root/packages/modules"/*; do
    [ -d "$child" ] || continue
    case "$(basename "$child")" in
        Gki|RuntimeI18n) ;;
        *) add_prune_marker "$child" ;;
    esac
done
for child in "$aosp_root/packages/modules/Gki"/*; do
    [ -d "$child" ] || continue
    case "$(basename "$child")" in
        build|libkver) ;;
        *) add_prune_marker "$child" ;;
    esac
done

# frameworks/base's root Blueprint pulls in the complete Java framework graph.
# The native camera targets only need module definitions from core/jni, media,
# and native/android. Finder markers do not hide source files from those kept
# modules; they only prevent unrelated Android.bp files from being discovered.
[ -f "$frameworks_base_bp" ] || fail "frameworks/base/Android.bp is missing"
[ -f "$frameworks_base_license_bp" ] ||
    fail "minimal frameworks/base license Blueprint is missing"
[ ! -e "$frameworks_base_bp_hidden" ] ||
    fail "temporary frameworks/base Blueprint path already exists"
mv -- "$frameworks_base_bp" "$frameworks_base_bp_hidden"
base_bp_hidden=1
cp -- "$frameworks_base_license_bp" "$frameworks_base_bp"

for child in "$frameworks_base"/*; do
    [ -d "$child" ] || continue
    case "$(basename "$child")" in
        core|media|native) ;;
        *) add_prune_marker "$child" ;;
    esac
done
for child in "$frameworks_base/core"/*; do
    [ -d "$child" ] || continue
    [ "$(basename "$child")" = jni ] || add_prune_marker "$child"
done
for child in "$frameworks_base/media"/*; do
    [ -d "$child" ] && add_prune_marker "$child"
done
for child in "$frameworks_base/native"/*; do
    [ -d "$child" ] || continue
    [ "$(basename "$child")" = android ] || add_prune_marker "$child"
done
add_prune_marker "$frameworks_base/native/android/tests"

cd "$aosp_root"
export OUT_DIR="$aosp_root/out/android-vcam-r34"
export ALLOW_MISSING_DEPENDENCIES=true
export WITH_DEXPREOPT=false
export DISABLE_PREOPT=true
# Android 12's RenderScript compiler still links ncurses ABI 5. Modern WSL
# distributions no longer ship it system-wide, but the tagged AOSP host
# toolchain includes the matching compatibility runtime. Expose only those two
# libraries: adding the whole legacy sysroot would also override the host libc.
compat_source="$aosp_root/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8/sysroot/usr/lib"
# llvm-rs-cc already carries a $ORIGIN/../lib64 RUNPATH. Put only the two
# compatibility links in that existing OUT_DIR host-runtime directory so the
# build does not depend on an inherited LD_LIBRARY_PATH.
compat_lib_dir="$OUT_DIR/soong/host/linux-x86/lib64"
mkdir -p "$compat_lib_dir"
ln -sfn "$(readlink -f "$compat_source/libncurses.so.5")" "$compat_lib_dir/libncurses.so.5"
ln -sfn "$(readlink -f "$compat_source/libtinfo.so.5")" "$compat_lib_dir/libtinfo.so.5"
source build/envsetup.sh >/dev/null
lunch aosp_arm64-eng >/dev/null
m -j"$jobs" WITH_DEXPREOPT=false \
    libcameraservice \
    camera.vcam \
    android.hardware.camera.provider@2.4-vcam-service

for artifact in \
    "$OUT_DIR/target/product/generic_arm64/system/lib64/libcameraservice.so" \
    "$OUT_DIR/target/product/generic_arm64/vendor/lib64/hw/camera.vcam.so" \
    "$OUT_DIR/target/product/generic_arm64/vendor/bin/hw/android.hardware.camera.provider@2.4-vcam-service"; do
    [ -s "$artifact" ] || fail "expected build artifact is missing: $artifact"
    printf 'Verified artifact: %s\n' "$artifact"
done

printf 'AOSP targets built successfully in %s\n' "$OUT_DIR"
'@

$script = $script.Replace("__AOSP_ROOT__", (ConvertTo-BashLiteral $AospRoot))
$script = $script.Replace("__SOURCE_ROOT__", (ConvertTo-BashLiteral $linuxRepoRoot))
$script = $script.Replace("__MODE__", (ConvertTo-BashLiteral $Mode))
$script = $script.Replace("__JOBS__", $Jobs.ToString([Globalization.CultureInfo]::InvariantCulture))

Invoke-WslBash -Script $script
