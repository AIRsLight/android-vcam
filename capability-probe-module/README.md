# Android Virtual Camera Compatibility Probe

This is a read-only Android 14 diagnostic module. It records the platform,
camera Provider transport, public camera topology and exact-profile match in:

```text
/data/adb/android_vcam_capability_probe/device-profile.conf
/data/adb/android_vcam_capability_probe/capability-result.conf
```

The module contains no `system`/`vendor` overlay, executable payload, Provider,
CameraService replacement, router, mount engine or SELinux policy. Its result
always keeps `routing_authorized=false` and `activation_policy=probe_only`.

The action button refreshes the report. Uninstalling removes only this probe's
state directory and does not touch Android VCAM configuration or its release
module.
