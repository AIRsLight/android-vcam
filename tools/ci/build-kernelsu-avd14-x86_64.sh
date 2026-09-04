#!/usr/bin/env bash
set -euo pipefail

kernel_root="${1:-/aosp/work/kernelsu-avd14}"
config_source="${2:-}"
common_commit=7e35917775b8b3e3346a87f294e334e258bf15e6
kernelsu_commit=932014ab5b2c9b74a3d11e2ec4d17dd10fc9442e
kernelsu_version=v3.3.0
clang_prebuilt_commit=9eb88323fb5dc0ef2eee886627631689e0949e9d

kernel_root="$(realpath -m "$kernel_root")"
case "$kernel_root" in
    /aosp/work/kernelsu-avd14) ;;
    *)
        echo "Refusing to modify unexpected kernel workspace: $kernel_root" >&2
        exit 1
        ;;
esac

if [[ -z "$config_source" ]]; then
    config_source="$(cd "$(dirname "$0")" && pwd)/kernelsu-avd14-x86_64.config"
fi
config_source="$(realpath "$config_source")"

[[ -d "$kernel_root/.repo" ]] || {
    echo "Kernel manifest workspace is missing: $kernel_root" >&2
    exit 1
}
[[ -f "$config_source" ]] || {
    echo "KernelSU config fragment is missing: $config_source" >&2
    exit 1
}

cd "$kernel_root"

if ! git -C common cat-file -e "$common_commit^{commit}" 2>/dev/null; then
    git -C common fetch --depth=1 aosp "$common_commit"
fi
git -C common checkout --detach --force "$common_commit"
if ! git -C prebuilts/clang/host/linux-x86 \
        cat-file -e "$clang_prebuilt_commit^{commit}" 2>/dev/null; then
    git -C prebuilts/clang/host/linux-x86 fetch --depth=1 aosp "$clang_prebuilt_commit"
fi
git -C prebuilts/clang/host/linux-x86 checkout --detach --force "$clang_prebuilt_commit"

# Backport the minimal host-build portions of AOSP changes
# 75f82c6a15c4 (resolve_btfids) and 42e503e28698 (objtool). Without these,
# glibc 2.38+ headers produce __isoc23_strto* references while the hermetic
# Android linker uses an older libc.
sed -i 's@$(MAKE) -C $(SUBCMD_SRC) OUTPUT=@$(MAKE) -C $(SUBCMD_SRC) EXTRA_CFLAGS="$(CFLAGS)" OUTPUT=@' \
    common/tools/bpf/resolve_btfids/Makefile
sed -i 's@$(MAKE) -C $(SUBCMD_SRCDIR) OUTPUT=@$(MAKE) -C $(SUBCMD_SRCDIR) EXTRA_CFLAGS="${CFLAGS}" OUTPUT=@' \
    common/tools/objtool/Makefile
grep -q 'SUBCMD_SRC) EXTRA_CFLAGS="$(CFLAGS)" OUTPUT=' \
    common/tools/bpf/resolve_btfids/Makefile
grep -q 'SUBCMD_SRCDIR) EXTRA_CFLAGS="${CFLAGS}" OUTPUT=' \
    common/tools/objtool/Makefile

if [[ ! -d KernelSU/.git ]]; then
    git clone https://github.com/tiann/KernelSU.git KernelSU
fi
if ! git -C KernelSU cat-file -e "$kernelsu_commit^{commit}" 2>/dev/null; then
    git -C KernelSU fetch --depth=1 origin "$kernelsu_commit"
fi
git -C KernelSU checkout --detach --force "$kernelsu_commit"

rm -f common/drivers/kernelsu common/kernelsu_defconfig
ln -s ../../KernelSU/kernel common/drivers/kernelsu

grep -q 'obj-$(CONFIG_KSU) += kernelsu/' common/drivers/Makefile ||
    printf '\nobj-$(CONFIG_KSU) += kernelsu/\n' >> common/drivers/Makefile
grep -q 'source "drivers/kernelsu/Kconfig"' common/drivers/Kconfig ||
    sed -i '/^endmenu/i source "drivers/kernelsu/Kconfig"' common/drivers/Kconfig
cp "$config_source" common/kernelsu_defconfig

tools/bazel build --config=fast \
    --//build/kernel/kleaf:defconfig_fragment=//common:kernelsu_defconfig \
    //common:kernel_x86_64_dist

grep -R -q '^CONFIG_KSU=y$' out/cache/*/common/.config
grep -R -q '^CONFIG_KSU_X86_PATCH_SYSCALL_DISPATCHER=y$' \
    out/cache/*/common/.config
prebuilts/clang/host/linux-x86/clang-r487747/bin/clang --version | \
    grep -q 'Android (9796371, based on r487747) clang version 17.0.0'

artifact_dir="$kernel_root/artifacts"
mkdir -p "$artifact_dir"
install -m 0644 bazel-bin/common/kernel_x86_64/bzImage \
    "$artifact_dir/kernel-ranchu-kernelsu-${kernelsu_version}-android14-6.1-x86_64"

artifact="$artifact_dir/kernel-ranchu-kernelsu-${kernelsu_version}-android14-6.1-x86_64"
sha256sum "$artifact"
file "$artifact"
git -C common rev-parse HEAD
git -C KernelSU rev-parse HEAD
