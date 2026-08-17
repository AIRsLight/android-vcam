# AIDL frontend

The AIDL provider targets Android 13 and newer. Android 13 exposes the stable
`android.hardware.camera.provider` and `android.hardware.camera.device`
interfaces, but does not provide an AIDL wrapper for a legacy Camera3 module
equivalent to the HIDL `camera.device@3.4-impl` used by this project.

The reference transport is Google's AIDL camera service and EmulatedCamera HWL
from `platform/hardware/google/camera`. The android-vcam frontend adds a
process-local HWL selector so the dedicated `vcam/0` service loads
`libvcam_googlecamerahwl_impl.so` without changing an OEM `internal/0`
provider. Its first functional layer wraps the upstream EmulatedCamera HWL,
maps delegate IDs 0/1 to collision-free IDs 1000/1001, and publishes the
provider-owned package vendor tag. Binder, FMQ, buffer-cache, fence and gralloc
semantics remain owned by the upstream implementation. Replacing the emulated
scene producer with the shared `FrameRenderer` is the next adapter layer.

Run the upstream transport baseline on the Android 13 reference tree with:

```bash
tools/verify-aosp13-aidl-baseline.sh \
    --aosp-root /aosp/src/android-13.0.0_r84 --jobs 8
```

The baseline command above verifies only the official upstream targets. Run
`tools/verify-aosp13-build.sh --mode build` to temporarily apply the versioned
Google Camera and CameraService patches and compile the android-vcam AIDL
service and HWL wrapper as well. The provider transport is selected from
`device-profile.conf`, and an AIDL service is never started on a device whose
framework only advertises HIDL providers.
