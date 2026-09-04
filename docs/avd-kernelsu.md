# KernelSU Android 14 AVD harness

The API 34 x86_64 AVD has a reproducible KernelSU kernel for exercising the
real module installer and lifecycle without changing the qualified
`vcam_aosp14_api34` AVD. This is a development harness, not a device-support
profile and not a release artifact.

## Pinned build

| Component | Revision |
| --- | --- |
| Android common kernel | `7e35917775b8b3e3346a87f294e334e258bf15e6` |
| Kernel line | Android 14 6.1 (`common-android14-6.1-2023-05-exp`) |
| KernelSU | v3.3.0, `932014ab5b2c9b74a3d11e2ec4d17dd10fc9442e` |
| Clang prebuilt | `9eb88323fb5dc0ef2eee886627631689e0949e9d` / r487747 |
| Kernel options | `CONFIG_KSU=y`, `CONFIG_KSU_X86_PATCH_SYSCALL_DISPATCHER=y` |

The Linux build host uses `/aosp/work/kernelsu-avd14`. Run the checked-in
helper from a checkout staged on that host:

```bash
tools/ci/build-kernelsu-avd14-x86_64.sh \
  /aosp/work/kernelsu-avd14 \
  tools/ci/kernelsu-avd14-x86_64.config \
  tools/ci/kernelsu-avd14-sepolicy-init.patch
```

The resulting `bzImage` is copied locally as:

```text
out/avd-kernels/kernel-ranchu-kernelsu-v3.3.0-android14-6.1-x86_64
```

The automatic-lifecycle build has SHA-256
`60a25489a7bc331672a10117236cf6e5c6b87d272f09e710ad634cecae160c9d`.
The earlier diagnostic-only build, before the SELinux initialization patch,
had SHA-256
`6cac31c2645f1e598acec109b08d9cab1c122f7b612c0c94bdb3becc2acd3e5`.

## Launch and verification

Use a separate AVD named `vcam_aosp14_ksu`. The launch helper deliberately
does not wipe its data unless `-WipeData` is supplied:

```powershell
pwsh -File tools/avd/start-aosp14-kernelsu.ps1
```

After placing the official x86_64 `ksud` at `/data/adb/ksud`, verify the
kernel interface and optionally install the report-only module:

```powershell
pwsh -File tools/avd/verify-aosp14-kernelsu.ps1 `
  -ModuleZip dist/android-vcam-capability-probe-v0.5.0-dev.39.zip
```

Reboot the AVD to exercise KernelSU's automatic `post-fs-data`, service and
boot-completed paths. `-RunBootEvents` remains available only as a diagnostic
fallback for the earlier unpatched kernel. The capability probe must still
report `activation_policy=probe_only`, `routing_authorized=false` and
`camera_mutation_performed=false`.

## x86_64 AVD limitation

KernelSU 3.3 initializes and its command-line client reads kernel version
`32601`. Module installation also succeeds. Two AVD-specific gaps remain:

- the Android x86_64 application seccomp profile kills the manager's initial
  `reboot(169)` driver-FD handshake, so the official Manager UI says
  `Not installed` even though root-side `ksud debug version` succeeds;
- the May 2023 6.1 baseline does not expose `x64_sys_call`, and unmodified
  KernelSU does not catch Android's second-stage init transition on this image.
  The checked-in kernel-build patch calls KernelSU's own policy initialization
  immediately after the initial SELinux policy load, allowing its injected init
  actions and `meta-overlayfs` to use the normal `ksu`/`ksu_file` labels.

The initial diagnostic build used the official `ksud` binary from ADB root to
drive boot events explicitly. The patched harness restores the automatic boot
lifecycle without changing the product manager or weakening the AVD's
application seccomp policy. The source patch is restricted to the pinned AVD
kernel build and is never carried into VCAM modules or ARM64 release code.
Consequently this setup validates module packaging and scripts, but does not
claim Manager-UI compatibility or exactly reproduce a phone's boot integration.

## Verified result

The report-only dev.39 archive completed KernelSU installation and all three
explicit lifecycle events. The resulting profile reported `root_manager=ksu`,
stable-AIDL provider v2, physical camera ID `1` and `global_only` as the future
candidate scope. The fail-closed result remained `probe_required`, with routing
unauthorized and no camera mutation.

With the module enabled, the regular test APK passed the public Camera1,
Camera2 and NDK CameraManager sequence. Camera1 index `0` and Camera2 ID `1`
both opened and closed successfully; the NDK probe reported one camera and one
readable characteristics set. CameraService still exposed exactly one stock
camera after the test.

The portable engineering packager also has an explicit `aosp14-avd` target.
It accepts only the pinned UE1A fingerprint, x86_64 ABI and recorded
CameraService/library hashes; the default `nx769j` target and its ARM64 guards
are unchanged. A pass-through package can be created with:

```powershell
pwsh -File tools/package-portable-bootstrap.ps1 `
  -TargetProfile aosp14-avd `
  -BootstrapMode passthrough `
  -Launcher out/avd-aosp14-x86_64/vcam_cameraserver_launcher `
  -Router out/avd-aosp14-x86_64/libvcam_cameraserver_router.so
```

This package requires an active MetaModule and automatically disables itself
for the following boot. It remains an engineering artifact and is never added
to the unified release manifest.

The pass-through archive was installed through the real KernelSU 3.3 module
installer with OverlayFS MetaModule 1.3.1. On the first reboot, the launcher
hash was active, the router library appeared in the cameraserver process maps,
and its complete protocol verdict became `probe_compatible`. The root-free test
application passed Camera1, Camera2 and NDK access. On the second reboot, the
module was disabled as designed, `/system/bin/cameraserver` returned to the
recorded stock hash, no router mapping remained, and the same public API test
passed again.
