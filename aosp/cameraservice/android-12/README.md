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

This is source-level ROM delivery. Applying the patch changes only the source
checkout and is reversible with the recorded patch. Flashing the resulting
system/vendor images is device-destructive compared with the existing APatch
adapter and requires the product's normal rollback or slot-switch procedure.
