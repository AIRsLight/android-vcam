# AIDL frontend

The AIDL provider targets Android 13 and newer. It will implement the stable
`android.hardware.camera.provider` and `android.hardware.camera.device`
interfaces while sharing `libvcam_frame_core` with the HIDL and legacy
frontends.

Implementation starts after the HIDL Device/Session contract is validated on
the Android 12 reference device. The provider transport is selected from the
generated `device-profile.conf`; an AIDL service is never started on a device
whose framework only advertises HIDL providers.
