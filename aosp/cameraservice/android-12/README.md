# Android 12 CameraService integration

This integration is pinned and patch-checked against AOSP tag
`android-12.0.0_r34`, `frameworks/av` commit `28c0056`. It keeps camera IDs
`1000` and `1001` internal while applications continue to enumerate and open
public IDs `0` and `1`.

At connect time CameraService resolves `(client package, requested camera ID)`
through `routes.tsv`. An enabled non-physical provider selects internal ID
`1000` or `1001`. Missing, invalid or stopped configured providers fail closed;
an application without a route continues to use the physical camera.

The patch also carries the verified client package inside provider-owned
session metadata. Injection happens in `Camera3Device`, so Camera1, Camera2,
CameraX and NDK clients share the same route once CameraService has authenticated
the caller.

Capability queries follow the same route as connection. Scoped Camera2 clients
receive the virtual device characteristics, while Camera1 info and legacy
parameters come from a separate virtual shim cache. This prevents applications
from selecting a physical-only resolution, format or frame-rate mode and then
opening a virtual device that cannot satisfy it. Concurrent-session checks are
routed as well.

Android 12 does not attach a package name to every pre-connect capability
query, so CameraService resolves the calling UID to its first package, matching
the platform's existing NDK attribution fallback. Products that place unrelated
applications under one shared UID must treat per-package pre-connect routing as
unsupported or add an attribution-aware framework extension.

## Apply

Place this repository somewhere inside the AOSP source tree, for example
`vendor/android_vcam`, then run from this repository on a Windows build host:

```powershell
pwsh -File tools/apply-aosp-cameraservice-patch.ps1 `
  -AospRoot D:\aosp -Mode Check
pwsh -File tools/apply-aosp-cameraservice-patch.ps1 `
  -AospRoot D:\aosp -Mode Apply
```

For Linux hosts, apply `frameworks-av.patch` from `frameworks/av` with
`git apply --check` followed by `git apply`.

Include `product/android_vcam.mk` from the product makefile and add the
repository's provider policy plus the product-specific cameraserver rule to
`BOARD_VENDOR_SEPOLICY_DIRS`. The cameraserver rule intentionally remains an
example because camera data type names differ between device policies.

Build and test at least `libcameraservice`, `camera.vcam`, and
`android.hardware.camera.provider@2.4-vcam-service` before enabling
`ro.vendor.vcam.provider.enabled=true` in a flashable image.

## Reproducible WSL build check

`tools/verify-aosp-build.ps1` validates the exact Android 12 checkout and runs
the three target builds through AOSP Soong. Its default `Check` mode is
read-only:

```powershell
pwsh -File tools/verify-aosp-build.ps1 `
  -WslDistro Ubuntu-26.04 `
  -AospRoot /home/user/aosp/android-12.0.0_r34
```

After the AOSP build dependencies have been synced, request the real build
explicitly:

```powershell
pwsh -File tools/verify-aosp-build.ps1 `
  -WslDistro Ubuntu-26.04 `
  -AospRoot /home/user/aosp/android-12.0.0_r34 `
  -Mode Build -Jobs 8
```

Build mode mirrors this repository into the marked, script-owned directory
`vendor/android_vcam_buildcheck`, uses the isolated output directory
`out/android-vcam-r34`, and temporarily applies the CameraService patch. A
shell trap reverses that patch on both success and build failure. The script
refuses to modify a dirty `frameworks/av` checkout or overwrite an unmarked
vendor directory. It does not run `repo sync`, flash images, use ADB, install an
APatch module or reboot a device.

The validator can operate on a native-focused partial checkout. During the
build it temporarily hides unrelated Blueprint trees and substitutes a minimal
`frameworks/base` root containing only the license and AIDL filegroups needed
by native camera dependencies. All markers and the original Blueprint are
restored by the same exit trap. On modern WSL distributions it also exposes the
AOSP-bundled ncurses 5 runtime only inside the isolated output directory for
Android 12's RenderScript compiler.

This path was compiled successfully against `android-12.0.0_r34` with
`aosp_arm64-eng`. The verified installed artifacts are:

- `system/lib64/libcameraservice.so`
- `vendor/lib64/hw/camera.vcam.so`
- `vendor/bin/hw/android.hardware.camera.provider@2.4-vcam-service`

This is source-level ROM delivery. Applying the patch changes only the source
checkout and is reversible with the recorded patch. Flashing the resulting
system/vendor images is device-destructive compared with the existing APatch
adapter and requires the product's normal rollback or slot-switch procedure.
