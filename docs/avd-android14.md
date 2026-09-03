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

## AIDL provider and global-route gate

The AOSP r23 stable-AIDL v2 provider was then staged as `vcam/0`. Two AVD-only
integration facts were found:

- the provider must be staged with the matching AOSP r23 Google camera HAL
  libraries; mixing the new HWL with the system image's older
  `libgooglecamerahal.so` silently bypasses the custom factory; and
- the emulator Google HWL normally ignores JSON camera configurations when
  `ro.boot.qemu=1`. The custom VCAM factory now explicitly enables its own
  configuration path without changing upstream emulator behavior for normal
  builds.

The direct AIDL probe reported interface version 2, two devices
`device@1.1/vcam/1000` and `device@1.1/vcam/1001`, and valid 640x480 YUV frames
from both. CameraService then held three raw devices (`10`, `1000`, `1001`),
while Camera2 and NDK clients continued to enumerate only public device `10`.

Public IDs are not assumed to be `0` and `1`. Camera2 IDs and Camera1 indices
also occupy separate namespaces, so they are never placed in the same routing
table. For this baseline, `targets.tsv` maps Camera2 ID `10` to logical back
target 0, while `camera1-targets.tsv` independently maps Camera1 index `0` to
that target. `camera1-map.tsv` is derived from CameraService's raw
`Device N maps to "ID"` table:

```text
10    0
1000  1
1001  2
```

This distinction is required because Camera1 `connect()` carries an integer
enumeration index, whereas Camera2 carries the string device ID. Sending
`1000` as a Camera1 index was caught by the protocol test and failed to
connect. With `camera1-map.tsv`, public Camera1 index `0` and public Camera2 ID
`10` both opened internal device `1000`; the virtual provider created a session
for each, and all router rewrite failures remained zero. Missing Camera1 map
entries now fail closed instead of falling through to a physical camera.

The portable `camera-map.sh` now performs this derivation automatically after
the provider and CameraService remain stable. It rejects duplicate IDs or
indices, missing internal devices, unexpected internal lens facing, and any
topology without a public back camera. It publishes the three maps plus
`topology.conf` only after the full dump validates. With a non-empty routing
policy, the Binder router rejects camera-scoped requests until that final
marker is readable. An AVD test confirmed Camera1, Camera2 characteristics and
NDK access were blocked before publication, with no physical or virtual open;
running the startup path then produced `10 -> back`, `0 -> back`,
`1000 -> Camera1 index 1` and `1001 -> Camera1 index 2` without restarting
cameraserver.

Finally, the ordinary preview activity requested only public ID `10` and
received the virtual color bars at about 30 fps. Its YUV analysis stream
advanced to 225 frames with full-range non-constant luminance (`min=0`,
`max=255`), while CameraService logs showed internal device `1000`. The
cameraserver and provider PIDs remained stable.

After qualification, the test app and provider were stopped, the VINTF
fragment and route files were removed, and the exact stock cameraserver
(`b40500255b1b9d51ea59d0dce3ac6c00fba81f605226297741d4a95ab849df99`)
was restored. After an Enforcing reboot, `media.camera` was present, only
physical device `10` remained, and no `vcam` service was registered.

## What this proves

The AVD proves that the same-process launcher, stable-AIDL v2 provider,
internal-ID hiding and a global public-to-virtual route work together on a clean
AOSP Android 14 stack. It covers Camera1, Camera2 and NDK clients and real frame
delivery through a public ID. It also removes both the relocated-executable and
hard-coded public-ID assumptions from the portable architecture.

It does not prove OEM compatibility, production SELinux policy installation,
MetaModule behavior, or non-pattern media sources on the AOSP provider. The
next generic gate is an Enforcing run with the complete module policy, followed
by testing the generated topology against the NX769J and another independently
implemented Android 14 camera stack.
