#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: build-mbedtls-android.sh --ndk-root PATH [options]

Build the pinned static Mbed TLS SDK used by FFmpeg HTTPS/HLS support.

Options:
  --abi ABI           arm64-v8a or x86_64 (default: arm64-v8a)
  --source-root PATH  Mbed TLS checkout (default: .reference/mbedtls)
  --output-root PATH  SDK output root (default: .reference/mbedtls-android/ABI)
  --api N             Android API level (default: 31)
  --jobs N            build parallelism (default: host CPU count)
  -h, --help          show this help
EOF
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)
ndk_root=
abi=arm64-v8a
source_root="$repo_root/.reference/mbedtls"
output_root=
api=31
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
mbedtls_tag=mbedtls-3.6.7

while (($#)); do
    case "$1" in
        --ndk-root) (($# >= 2)) || fail "--ndk-root requires a value"; ndk_root=$2; shift 2 ;;
        --abi) (($# >= 2)) || fail "--abi requires a value"; abi=$2; shift 2 ;;
        --source-root) (($# >= 2)) || fail "--source-root requires a value"; source_root=$2; shift 2 ;;
        --output-root) (($# >= 2)) || fail "--output-root requires a value"; output_root=$2; shift 2 ;;
        --api) (($# >= 2)) || fail "--api requires a value"; api=$2; shift 2 ;;
        --jobs) (($# >= 2)) || fail "--jobs requires a value"; jobs=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) fail "unknown argument: $1" ;;
    esac
done

[[ -n "$ndk_root" ]] || fail "--ndk-root is required"
[[ "$abi" == arm64-v8a || "$abi" == x86_64 ]] ||
    fail "--abi must be arm64-v8a or x86_64"
[[ "$api" =~ ^[0-9]+$ && "$api" -ge 21 ]] || fail "--api must be at least 21"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail "--jobs must be positive"
[[ "$(uname -s)" == Linux ]] || fail "the Mbed TLS cross-build requires a Linux host"

ndk_root=$(cd -- "$ndk_root" && pwd)
toolchain="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64"
bin="$toolchain/bin"
case "$abi" in
    arm64-v8a) target_triple=aarch64-linux-android ;;
    x86_64) target_triple=x86_64-linux-android ;;
esac
cc="$bin/${target_triple}${api}-clang"
[[ -x "$cc" ]] || fail "Android $abi Clang wrapper is missing: $cc"
if [[ -z "$output_root" ]]; then
    output_root="$repo_root/.reference/mbedtls-android/$abi"
fi

if [[ ! -e "$source_root/.git" ]]; then
    [[ ! -e "$source_root" ]] || fail "source path exists but is not a Git checkout: $source_root"
    mkdir -p "$(dirname -- "$source_root")"
    git clone --depth 1 --branch "$mbedtls_tag" --recurse-submodules \
        https://github.com/Mbed-TLS/mbedtls.git "$source_root"
fi
source_root=$(cd -- "$source_root" && pwd)
actual_tag=$(git -C "$source_root" describe --tags --exact-match 2>/dev/null || true)
[[ "$actual_tag" == "$mbedtls_tag" || "$actual_tag" == v3.6.7 ]] ||
    fail "Mbed TLS checkout must be exactly $mbedtls_tag (found $actual_tag)"
if [[ ! -f "$source_root/framework/exported.make" ]]; then
    git -C "$source_root" submodule update --init --depth 1
fi

make -C "$source_root/library" clean
make -C "$source_root/library" -j"$jobs" \
    CC="$cc" AR="$bin/llvm-ar" AR_DASH= \
    CFLAGS='-O2 -fPIC' WARNING_CFLAGS='-Wall -Wextra'

mkdir -p "$output_root/include" "$output_root/lib"
rm -rf -- "$output_root/include/mbedtls" "$output_root/include/psa"
cp -R "$source_root/include/mbedtls" "$output_root/include/"
cp -R "$source_root/include/psa" "$output_root/include/"
cp "$source_root/library/libmbedtls.a" \
   "$source_root/library/libmbedx509.a" \
   "$source_root/library/libmbedcrypto.a" \
   "$output_root/lib/"

for archive in libmbedtls.a libmbedx509.a libmbedcrypto.a; do
    [[ -s "$output_root/lib/$archive" ]] || fail "missing static archive: $archive"
done
{
    printf 'mbedtls_tag=%s\n' "$mbedtls_tag"
    printf 'android_api=%s\n' "$api"
    printf 'abi=%s\n' "$abi"
} > "$output_root/build-info.txt"

printf 'Built static Android Mbed TLS SDK: %s\n' "$output_root"
