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
Provider transport/version/instance, legacy camera module path and hashes.
`adapter_hint` selects the first integration candidate:

| Hint | Intended frontend |
| --- | --- |
| `legacy-camera-module` | Compatibility proxy around an OEM `camera.*.so` |
| `hidl-provider-service` | Standalone Android 8-12 HIDL provider |
| `aosp-aidl` | Standalone Android 13+ stable-AIDL provider |
| `unsupported` | No known Camera Provider transport was discovered |

The profile is diagnostic input, not authorization to install. An adapter
must still validate the exact binaries and product policy it will interact
with. Unknown builds must fail closed and leave physical cameras untouched.

## Support tiers

1. **AOSP frontend** — built into a ROM or vendor image with matching VINTF,
   init and SELinux policy. This is the portable long-term path.
2. **OEM compatibility adapter** — systemless APatch delivery for devices
   whose camera stack only exposes a proprietary legacy module. Each adapter
   owns its binary signatures and vendor quirks.
3. **Unsupported** — no runtime patching is attempted when neither frontend
   has been validated.

The current OnePlus 7 Pro module remains a tier-2 adapter. The initial HIDL
service in `aosp/provider/hidl` is a discovery-safe tier-1 skeleton: it
registers `vcam/0` but intentionally exposes no virtual camera until the
Device/Session implementation is complete.
