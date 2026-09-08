# Unified root module

This template is the only root-module surface published to users. Its installer
requires an active compatible MetaModule under KernelSU or APatch and copies
only the selected profile into the final `android_vcam` module tree.

Exact OnePlus 7 Pro Android 12 and NX769J Android 14 profiles take priority.
Starting with dev.44, other eligible OnePlus Qualcomm Android 10–14 builds use
the experimental global Camera HAL shim: ARM64, camera.qcom.so, the local_time
snapshot slot and an unambiguous physical HIDL 2.4 service_64 declaration are
required. Other layouts are rejected. Detection establishes eligibility, not
hardware qualification. No runtime disassembly or OEM virtual-provider change
is performed.

The generic profile snapshots the device's own HAL during installation and
preserves the verified snapshot on same-firmware upgrades. A failed post-mount
check disables the next boot; it does not undo an already active overlay or
establish automatic current-boot recovery. See the community testing guide.

Device-specific module archives remain build inputs only. They are expanded
under `payload/profiles` by `tools/package-unified-module.ps1` and are not
published as independent modules.
