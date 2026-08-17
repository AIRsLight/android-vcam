#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: sync-aosp13-build-deps.sh --aosp-root PATH [options]

Synchronize the supplemental projects used by the Android 13 camera target
validator after the base manifest groups have been initialized.

Options:
  --jobs N             repo sync parallelism (default: 4)
  --proxy-url URL      HTTP(S) proxy exported only for this invocation
  --force-sync         allow repo to replace conflicting project git dirs
  -h, --help           show this help
EOF
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

aosp_root=
jobs=4
proxy_url=
force_sync=0

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
        --proxy-url)
            (($# >= 2)) || fail "--proxy-url requires a value"
            proxy_url=$2
            shift 2
            ;;
        --force-sync)
            force_sync=1
            shift
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
[[ -d "$aosp_root/.repo" ]] || fail "not an initialized repo checkout: $aosp_root"
command -v repo >/dev/null 2>&1 || fail "repo is not available on PATH"

if [[ -n "$proxy_url" ]]; then
    export http_proxy=$proxy_url
    export https_proxy=$proxy_url
    export HTTP_PROXY=$proxy_url
    export HTTPS_PROXY=$proxy_url
fi

# This is the supplemental dependency closure encountered by the Soong-only
# aosp_arm64 camera build. The base platform checkout still comes from the
# official android-13.0.0_r84 manifest and the groups documented in
# docs/development.md.
projects=(
    platform/external/boringssl
    platform/external/icu
    platform/external/jsilver
    platform/external/jsoncpp
    platform/external/libcxx
    platform/external/libcxxabi
    platform/external/libexif
    platform/external/libjpeg-turbo
    platform/external/libogg
    platform/external/libphonenumber
    platform/external/libvpx
    platform/external/libwebm
    platform/external/libxml2
    platform/external/libyuv
    platform/external/llvm
    platform/external/lzma
    platform/external/modp_b64
    platform/external/okhttp
    platform/external/pcre
    platform/external/protobuf
    platform/external/rappor
    platform/external/scudo
    platform/external/selinux
    platform/external/sonivox
    platform/external/speex
    platform/external/starlark-go
    platform/external/tagsoup
    platform/external/wayland
    platform/external/wayland-protocols
    platform/external/zlib
    platform/frameworks/compile/libbcc
    platform/frameworks/compile/slang
    platform/frameworks/hardware/interfaces
    platform/frameworks/rs
    platform/hardware/google/interfaces
    platform/libcore
    platform/libnativehelper
    platform/packages/modules/common
    platform/packages/modules/Media
    platform/packages/modules/RuntimeI18n
    platform/prebuilts/clang-tools
    platform/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8
    platform/prebuilts/module_sdk/Media
    platform/system/libbase
    platform/system/libfmq
    platform/system/libhwbinder
    platform/system/libprocinfo
    platform/system/libvintf
    platform/system/logging
    platform/system/media
    platform/system/memory/libdmabufheap
    platform/system/memory/libion
    platform/system/tools/aidl
    platform/system/tools/sysprop
    platform/system/unwinding
    platform/test/app_compat/csuite
    platform/tools/metalava
    tools/platform-compat
)

sync_args=(
    sync
    -c
    --no-tags
    --no-clone-bundle
    -j"$jobs"
)
if ((force_sync)); then
    sync_args+=(--force-sync)
fi

cd "$aosp_root"
repo "${sync_args[@]}" "${projects[@]}"
printf 'Android 13 supplemental build dependencies synchronized.\n'
