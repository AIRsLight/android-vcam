# Android 14 AVD qualification

This is the AOSP platform/API baseline, not an OEM device profile. It exists to
separate Android-version behavior from NX769J vendor behavior.

## Baseline

| Field | Value |
| --- | --- |
| Qualification date | 2026-09-03 |
| AVD | `vcam_aosp14_api34` |
| System image | `system-images;android-34;default;x86_64` revision 4 |
| Android / SDK | 14 / 34 |
| Build fingerprint | `Android/sdk_phone64_x86_64/emu64x:14/UE1A.230829.036.A1/11228894:userdebug/test-keys` |
| Public Camera2 IDs | `10` |
| Camera1 back index | `0` |

The x86_64 launcher, router and Android 14 provider targets were built against
the pinned AOSP `android-14.0.0_r23` tree. The same validator also runs the
CameraService profile, protocol-evidence and Parcel-observer host tests:

```text
tools/verify-aosp14-build.sh --aosp-root /path/to/aosp \
  --mode build --product aosp_x86_64
```

## Public-client baseline

The dual-ABI test APK was installed with its camera permission granted and
launched through `io.github.androidvcam.test.RUN_PROTOCOL_PROBE`.

- the x86_64 NDK probe returned one camera and one characteristics record;
- the Java concurrency query returned zero combinations;
- invalid-ID torch requests reached CameraService and were safely rejected;
- Camera1 opened and closed back-facing API1 device 0; and
- Camera2 opened and closed public device 10.

No application crash, cameraserver restart or stuck camera owner was observed.

## Bootstrap discovery

The first launcher design entered `u:r:cameraserver:s0` and then attempted to
execute a captured binary from `/system/bin/vcam/cameraserver`. Standard AOSP
policy denied `execute_no_trans`; the platform policy also contains a
corresponding `neverallow`. Adding an OEM-style allow rule is therefore not a
valid generic AOSP design.

The launcher now remains in the process started by init, optionally loads the
router with `dlopen()`, loads the device's own `libcameraservice.so`, resolves
`CameraService::instantiate()`, and mirrors the Android 14
`main_cameraserver` Binder/HIDL startup sequence. The captured stock executable
is retained only for external recovery and is never executed by cameraserver.

With SELinux Enforcing, `stock` mode started this in-process CameraService,
published `media.camera`, and enumerated device 10. The launcher hash was
`8d6ed0073b6cb6bee9be775dfbe66d9e80e87af531c695f24044546c9ddb17e0`.

## Router gates

Directly copying files into a writable AVD system image does not install the
root module's `sepolicy.rule`. Under Enforcing, creation of the guarded marker
in `/dev/vcam` was therefore denied. The launcher failed open to the unrouted
CameraService, kept `media.camera` available, did not map the router and left no
pending marker. This is the expected safe-fallback result, not a router-policy
qualification.

SELinux was then made Permissive only for the disposable AVD functional test:

1. `preflight` mapped the router, verified the original local CameraService
   Binder and cleared the pending marker without replacing the service;
2. `passthrough` replaced only the `media.camera` registration with the local
   forwarding Binder;
3. the protocol probe reached verdict `probe_compatible`, with required, seen
   and valid masks all equal to `0x0000000000000f6f`, and zero invalid,
   unsupported or rejected transactions;
4. Camera1, Camera2 and NDK calls completed while the cameraserver PID remained
   unchanged; and
5. a continuous Camera2 preview of device 10 rendered the emulator virtual
   scene at about 30 fps while the YUV analysis stream advanced to 180 frames
   with non-constant luminance (`avg=100`, `min=16`, `max=225`).

After qualification, the test app was stopped, the exact stock cameraserver
(`b40500255b1b9d51ea59d0dce3ac6c00fba81f605226297741d4a95ab849df99`)
was restored to `/system/bin/cameraserver`, mode was set to `stock`, SELinux was
returned to Enforcing, and `media.camera` plus closed device 10 were verified.

## What this proves

The AVD proves that the same-process launcher and Binder router work on a clean
AOSP Android 14 camera stack and that the pass-through path preserves real
preview frames. It also removed the relocated-executable assumption from the
portable architecture.

It does not prove OEM compatibility, production SELinux policy installation,
MetaModule behavior, virtual provider registration, ID rewriting or frame
replacement. The next generic gate is an Enforcing run with the complete module
policy, followed by AIDL provider registration and one global physical-ID route
on the AOSP baseline.
