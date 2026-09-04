#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: build-ffmpeg-android.sh --ndk-root PATH [options]

Build a pinned, static FFmpeg SDK for the Android vcam-streamer.

Options:
  --abi ABI           arm64-v8a or x86_64 (default: arm64-v8a)
  --source-root PATH  FFmpeg checkout (default: .reference/ffmpeg)
  --output-root PATH  SDK output root (default: .reference/ffmpeg-android/ABI)
  --api N             Android API level (default: 31)
  --jobs N            make parallelism (default: host CPU count)
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
source_root="$repo_root/.reference/ffmpeg"
output_root=
api=31
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
ffmpeg_tag=n4.2.2

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
[[ "$(uname -s)" == Linux ]] || fail "the FFmpeg cross-build requires a Linux host"

ndk_root=$(cd -- "$ndk_root" && pwd)
toolchain="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64"
bin="$toolchain/bin"
case "$abi" in
    arm64-v8a)
        target_arch=aarch64
        target_cpu=armv8-a
        target_triple=aarch64-linux-android
        abi_configure_flags=()
        ;;
    x86_64)
        target_arch=x86_64
        target_cpu=x86-64
        target_triple=x86_64-linux-android
        # Keep the AVD/CI build independent from a host NASM package. The
        # production ARM64 build is unchanged; x86_64 is a test harness.
        abi_configure_flags=(--disable-x86asm)
        ;;
esac
cc="$bin/${target_triple}${api}-clang"
cxx="$bin/${target_triple}${api}-clang++"
[[ -x "$cc" && -x "$cxx" ]] || fail "Android $abi Clang wrappers are missing under $bin"
if [[ -z "$output_root" ]]; then
    output_root="$repo_root/.reference/ffmpeg-android/$abi"
fi

if [[ ! -e "$source_root/.git" ]]; then
    [[ ! -e "$source_root" ]] || fail "source path exists but is not a Git checkout: $source_root"
    mkdir -p "$(dirname -- "$source_root")"
    git clone --depth 1 --branch "$ffmpeg_tag" \
        https://git.ffmpeg.org/ffmpeg.git "$source_root"
fi
source_root=$(cd -- "$source_root" && pwd)
actual_tag=$(git -C "$source_root" describe --tags --exact-match 2>/dev/null || true)
[[ "$actual_tag" == "$ffmpeg_tag" ]] || fail "FFmpeg checkout must be exactly $ffmpeg_tag (found $actual_tag)"

build_root="$repo_root/.reference/ffmpeg-build/android-${abi}-api${api}"
mkdir -p "$build_root" "$output_root"
build_root=$(cd -- "$build_root" && pwd)
output_parent=$(cd -- "$(dirname -- "$output_root")" && pwd)
output_root="$output_parent/$(basename -- "$output_root")"

cd "$build_root"
if [[ -f Makefile ]]; then
    make distclean >/dev/null
fi

"$source_root/configure" \
    --prefix="$output_root" \
    --target-os=android \
    --arch="$target_arch" \
    --cpu="$target_cpu" \
    --enable-cross-compile \
    --sysroot="$toolchain/sysroot" \
    --cc="$cc" \
    --cxx="$cxx" \
    --ar="$bin/llvm-ar" \
    --nm="$bin/llvm-nm" \
    --ranlib="$bin/llvm-ranlib" \
    --strip="$bin/llvm-strip" \
    --disable-shared \
    --enable-static \
    --enable-pic \
    --enable-small \
    --enable-network \
    --disable-programs \
    --disable-doc \
    --disable-debug \
    --disable-avdevice \
    --disable-avfilter \
    --disable-postproc \
    --disable-encoders \
    --disable-muxers \
    --disable-indevs \
    --disable-outdevs \
    --disable-hwaccels \
    --disable-mediacodec \
    --disable-iconv \
    --disable-bzlib \
    --disable-lzma \
    --disable-symver \
    --extra-cflags='-fPIC' \
    --extra-ldflags='-Wl,-z,max-page-size=16384' \
    "${abi_configure_flags[@]}"

make -j"$jobs"
make install

for archive in libavformat.a libavcodec.a libswscale.a libswresample.a libavutil.a; do
    [[ -s "$output_root/lib/$archive" ]] || fail "missing static archive: $archive"
done
"$bin/llvm-nm" -g --defined-only "$output_root/lib/libavformat.a" | \
    grep 'ff_rtsp_demuxer' >/dev/null || fail "built libavformat does not contain the RTSP demuxer"

{
    printf 'ffmpeg_tag=%s\n' "$ffmpeg_tag"
    printf 'android_api=%s\n' "$api"
    printf 'target=%s\n' "$target_triple"
    printf 'configuration='
    sed -n 's/^FFMPEG_CONFIGURATION=//p' ffbuild/config.mak
} > "$output_root/build-info.txt"

printf 'Built static Android FFmpeg SDK: %s\n' "$output_root"
