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
  tools/ci/kernelsu-avd14-x86_64.config
```

The resulting `bzImage` is copied locally as:

```text
out/avd-kernels/kernel-ranchu-kernelsu-v3.3.0-android14-6.1-x86_64
```

The first qualified build had SHA-256
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
  -ModuleZip dist/android-vcam-capability-probe-v0.5.0-dev.39.zip `
  -RunBootEvents
```

This exercises KernelSU's archive extraction, `customize.sh`, module database,
`post-fs-data`, service and boot-completed paths. The capability probe must
still report `activation_policy=probe_only`, `routing_authorized=false` and
`camera_mutation_performed=false`.

## x86_64 AVD limitation

KernelSU 3.3 initializes and its command-line client reads kernel version
`32601`. Module installation also succeeds. Two AVD-specific gaps remain:

- the Android x86_64 application seccomp profile kills the manager's initial
  `reboot(169)` driver-FD handshake, so the official Manager UI says
  `Not installed` even though root-side `ksud debug version` succeeds;
- the May 2023 6.1 baseline does not expose `x64_sys_call`, and KernelSU does
  not catch Android's second-stage init transition on this image. Its injected
  init actions therefore cannot enter `u:r:ksu:s0` automatically.

The harness uses the official `ksud` binary from ADB root to drive boot events
explicitly. We do not patch the product manager, weaken the AVD's application
seccomp policy, or carry AVD-only SELinux shortcuts into ARM64 release code.
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
