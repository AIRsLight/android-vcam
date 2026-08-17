#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: build-ffmpeg-android.sh --ndk-root PATH [options]

Build a pinned, static FFmpeg SDK for the arm64 Android vcam-streamer.

Options:
  --source-root PATH  FFmpeg checkout (default: .reference/ffmpeg)
  --output-root PATH  SDK output root (default: .reference/ffmpeg-android/arm64-v8a)
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
source_root="$repo_root/.reference/ffmpeg"
output_root="$repo_root/.reference/ffmpeg-android/arm64-v8a"
api=31
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
ffmpeg_tag=n4.2.2

while (($#)); do
    case "$1" in
        --ndk-root) (($# >= 2)) || fail "--ndk-root requires a value"; ndk_root=$2; shift 2 ;;
        --source-root) (($# >= 2)) || fail "--source-root requires a value"; source_root=$2; shift 2 ;;
        --output-root) (($# >= 2)) || fail "--output-root requires a value"; output_root=$2; shift 2 ;;
        --api) (($# >= 2)) || fail "--api requires a value"; api=$2; shift 2 ;;
        --jobs) (($# >= 2)) || fail "--jobs requires a value"; jobs=$2; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) fail "unknown argument: $1" ;;
    esac
done

[[ -n "$ndk_root" ]] || fail "--ndk-root is required"
[[ "$api" =~ ^[0-9]+$ && "$api" -ge 21 ]] || fail "--api must be at least 21"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail "--jobs must be positive"
[[ "$(uname -s)" == Linux ]] || fail "the FFmpeg cross-build requires a Linux host"

ndk_root=$(cd -- "$ndk_root" && pwd)
toolchain="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64"
bin="$toolchain/bin"
cc="$bin/aarch64-linux-android${api}-clang"
cxx="$bin/aarch64-linux-android${api}-clang++"
[[ -x "$cc" && -x "$cxx" ]] || fail "Android arm64 Clang wrappers are missing under $bin"

if [[ ! -e "$source_root/.git" ]]; then
    [[ ! -e "$source_root" ]] || fail "source path exists but is not a Git checkout: $source_root"
    mkdir -p "$(dirname -- "$source_root")"
    git clone --depth 1 --branch "$ffmpeg_tag" \
        https://git.ffmpeg.org/ffmpeg.git "$source_root"
fi
source_root=$(cd -- "$source_root" && pwd)
actual_tag=$(git -C "$source_root" describe --tags --exact-match 2>/dev/null || true)
[[ "$actual_tag" == "$ffmpeg_tag" ]] || fail "FFmpeg checkout must be exactly $ffmpeg_tag (found $actual_tag)"

build_root="$repo_root/.reference/ffmpeg-build/android-arm64-v8a-api${api}"
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
    --arch=aarch64 \
    --cpu=armv8-a \
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
    --extra-ldflags='-Wl,-z,max-page-size=16384'

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
    printf 'target=aarch64-linux-android\n'
    printf 'configuration='
    sed -n 's/^FFMPEG_CONFIGURATION=//p' ffbuild/config.mak
} > "$output_root/build-info.txt"

printf 'Built static Android FFmpeg SDK: %s\n' "$output_root"
