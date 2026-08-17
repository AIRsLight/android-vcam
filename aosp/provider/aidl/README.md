# AIDL frontend

The AIDL provider targets Android 13 and newer. Android 13 exposes the stable
`android.hardware.camera.provider` and `android.hardware.camera.device`
interfaces, but does not provide an AIDL wrapper for a legacy Camera3 module
equivalent to the HIDL `camera.device@3.4-impl` used by this project.

The reference transport is Google's AIDL camera service and EmulatedCamera HWL
from `platform/hardware/google/camera`. The android-vcam frontend will add a
small HWL adapter at that boundary so Binder, FMQ, buffer-cache, fence and
gralloc semantics remain owned by the upstream implementation. The adapter
will expose IDs 1000/1001, publish the provider-owned package vendor tag, read
the session route, and feed buffers through the shared `FrameRenderer`.

Run the upstream transport baseline on the Android 13 reference tree with:

```bash
tools/verify-aosp13-aidl-baseline.sh \
    --aosp-root /aosp/src/android-13.0.0_r84 --jobs 8
```

This baseline builds and verifies the official AIDL service and emulated HWL;
it is not a functional android-vcam provider by itself. The provider transport
is selected from `device-profile.conf`, and an AIDL service is never started
on a device whose framework only advertises HIDL providers.
