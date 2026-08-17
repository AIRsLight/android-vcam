#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: sync-aosp14-build-deps.sh --aosp-root PATH [options]

Synchronize the supplemental projects used by the Android 14 camera target
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

# Supplemental dependency closure encountered by the Soong-only aosp_arm64
# build of CameraService and the stable-AIDL v2 provider at android-14.0.0_r23.
# Keep this explicit: the helper must not silently expand into a full platform
# checkout when the camera target graph changes.
projects=(
    kernel/configs
    platform/external/boringssl
    platform/external/golang-protobuf
    platform/external/icu
    platform/external/jsilver
    platform/external/jsoncpp
    platform/external/kotlinc
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
    platform/external/ow2-asm
    platform/external/pandora/avatar
    platform/external/pandora/bt-test-interfaces
    platform/external/pandora/mmi2grpc
    platform/external/pcre
    platform/external/protobuf
    platform/external/rust/crates/rustc-demangle
    platform/external/rust/crates/rustc-demangle-capi
    platform/external/python/cpython3
    platform/external/rappor
    platform/external/scudo
    platform/external/selinux
    platform/external/skia
    platform/external/sonivox
    platform/external/spdx-tools
    platform/external/sqlite
    platform/external/speex
    platform/external/starlark-go
    platform/external/tagsoup
    platform/external/tinyxml2
    platform/external/vulkan-headers
    platform/external/wayland
    platform/external/wayland-protocols
    platform/external/zlib
    platform/frameworks/base
    platform/frameworks/compile/libbcc
    platform/frameworks/compile/slang
    platform/frameworks/hardware/interfaces
    platform/frameworks/libs/net
    platform/frameworks/native
    platform/frameworks/rs
    platform/hardware/google/camera
    platform/hardware/google/interfaces
    platform/hardware/interfaces
    platform/hardware/libhardware
    platform/libcore
    platform/libnativehelper
    platform/packages/modules/common
    platform/packages/modules/AdServices
    platform/packages/modules/AppSearch
    platform/packages/modules/Bluetooth
    platform/packages/modules/ConfigInfrastructure
    platform/packages/modules/Connectivity
    platform/packages/modules/DeviceLock
    platform/packages/modules/HealthFitness
    platform/packages/modules/IPsec
    platform/packages/modules/Media
    platform/packages/modules/OnDevicePersonalization
    platform/packages/modules/Permission
    platform/packages/modules/RemoteKeyProvisioning
    platform/packages/modules/RuntimeI18n
    platform/packages/modules/Scheduling
    platform/packages/modules/SdkExtensions
    platform/packages/modules/StatsD
    platform/packages/modules/Uwb
    platform/packages/modules/Virtualization
    platform/packages/modules/Wifi
    platform/packages/providers/MediaProvider
    platform/prebuilts/abi-dumps/platform
    platform/prebuilts/bazel/common
    platform/prebuilts/bazel/linux-x86_64
    platform/prebuilts/build-tools
    platform/prebuilts/clang-tools
    platform/prebuilts/clang/host/linux-x86
    platform/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8
    platform/prebuilts/go/linux-x86
    platform/prebuilts/gradle-plugin
    platform/prebuilts/jdk/jdk11
    platform/prebuilts/jdk/jdk17
    platform/prebuilts/module_sdk/art
    platform/prebuilts/module_sdk/Media
    platform/prebuilts/misc
    platform/prebuilts/rust
    platform/prebuilts/sdk
    platform/prebuilts/tools
    platform/prebuilts/vndk/v29
    platform/prebuilts/vndk/v30
    platform/prebuilts/vndk/v31
    platform/prebuilts/vndk/v32
    platform/prebuilts/vndk/v33
    platform/system/apex
    platform/system/bpf
    platform/system/core
    platform/system/hardware/interfaces
    platform/system/libbase
    platform/system/libfmq
    platform/system/libhidl
    platform/system/libziparchive
    platform/system/libhwbinder
    platform/system/libprocinfo
    platform/system/libvintf
    platform/system/logging
    platform/system/media
    platform/system/memory/libion
    platform/system/memory/libdmabufheap
    platform/system/memory/libmeminfo
    platform/system/server_configurable_flags
    platform/system/memory/libmemtrack
    platform/system/memory/libmemunreachable
    platform/system/sepolicy
    platform/system/tools/aidl
    platform/system/tools/hidl
    platform/system/tools/sysprop
    platform/system/tools/xsdc
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
    --optimized-fetch
    --prune
    -j"$jobs"
)
if ((force_sync)); then
    sync_args+=(--force-sync)
fi

cd "$aosp_root"
repo "${sync_args[@]}" "${projects[@]}"
printf 'Android 14 supplemental build dependencies synchronized.\n'
