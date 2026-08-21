# Device support model

The portable implementation separates the common frame engine and control
plane from the camera transport used by a particular Android release. Device
support is selected from a generated capability profile instead of product
name checks scattered through native code.

Run the read-only probe with:

```powershell
pwsh -File tools/probe-device.ps1
```

The profile records the Android version and ABI, SELinux state, Camera
Provider transport/version/instance, VINTF fragment, init service and SELinux
labels, Camera2 IDs, Camera1 mappings, provider executable, legacy camera module
path and hashes. Schema 4 additionally hashes `libcameraservice.so` and
`libcamera_client.so`, then emits `profile_id`, `profile_status`, `route_scope`
and reserved virtual IDs for exact qualified recipes. The host helper uses an already-authorized ADB `su` context
when available so vendor files hidden from the shell UID can still be hashed;
the probe remains read-only.
`adapter_hint` selects the first integration candidate:

| Hint | Intended frontend |
| --- | --- |
| `legacy-camera-module` | Compatibility proxy around an OEM `camera.*.so` |
| `hidl-provider-service` | Standalone Android 8-12 HIDL provider |
| `aosp-aidl` | Standalone Android 13+ stable-AIDL provider |
| `unsupported` | No known Camera Provider transport was discovered |

Stable-AIDL discovery is instance-neutral. Qualcomm names such as
`vendor_qti/0` and AOSP names such as `internal/0` are both detected from the
service manager, then matched against the device VINTF manifest to obtain the
declared interface version.

The profile is diagnostic input, not authorization to install. An adapter
must still validate the exact binaries and product policy it will interact
with. Unknown builds must fail closed and leave physical cameras untouched.

`camera_ids` is the complete Camera2-facing set reported by CameraService;
`api1_camera_ids` is the smaller ordered set exposed through Camera1. Extra
Camera2 IDs are treated as separate physical access paths for privacy analysis,
even when the manager initially exposes only front/back targets.

## Support tiers

1. **AOSP frontend** — built into a ROM or vendor image with matching VINTF,
   init and SELinux policy. This is the portable long-term path.
2. **OEM compatibility adapter** — systemless APatch delivery for devices
   whose camera stack only exposes a proprietary legacy module. Each adapter
   owns its binary signatures and vendor quirks.
3. **Unsupported** — no runtime patching is attempted when neither frontend
   has been validated.

The current OnePlus 7 Pro module remains a tier-2 adapter. The HIDL service in
`aosp/provider/hidl` is the first tier-1 implementation: it registers
`vcam/0`, exposes collision-free virtual IDs 1000/1001 and relies on AOSP's
Camera3-to-HIDL Device/Session implementation. The Android 12 CameraService
patch under `aosp/cameraservice/android-12` performs app-scoped redirection
from physical target IDs to these internal devices and carries the package
identity in provider-owned session metadata. Pre-connect capability queries
are routed to the same internal ID, so advertised stream configurations match
the device that will actually be opened.

## Qualified profiles

- [Nubia NX769J Android 14](device-profiles/nx769j-android14.md) — exact UKQ1
  CameraService and `libcamera_client` ABI, stable-AIDL v2 provider, hidden
  internal devices 1000/1001, scoped public-ID routing, RTSP delivery, delayed
  network recovery and automatic stock rollback are qualified. The root-free
  Manager recognizes and cross-checks this profile; other NX769J builds remain
  unqualified until their camera ABI is reprobed.
