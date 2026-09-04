# Android 14 AVD qualification

This is the AOSP platform/API baseline, not an OEM device profile. It exists to
separate Android-version behavior from NX769J vendor behavior.

## Baseline

| Field | Value |
| --- | --- |
| Qualification date | 2026-09-04 |
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

## Initial system-image staging gate

Directly copying files into a writable AVD system image does not install the
root module's `sepolicy.rule`. Under Enforcing, creation of the guarded marker
in `/dev/vcam` was therefore denied. The launcher failed open to the unrouted
CameraService, kept `media.camera` available, did not map the router and left no
pending marker. This is the expected safe-fallback result, not a router-policy
qualification.

SELinux was initially made Permissive only for the disposable AVD functional
bring-up. This result was later superseded by the complete Enforcing gate below:

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

## SELinux Enforcing qualification

The final run kept the AVD Enforcing and launched the provider from init in
`u:r:hal_camera_default:s0`; the same-process router remained in
`u:r:cameraserver:s0`. A systemless VINTF fragment cannot retroactively update
servicemanager's boot-time `service_contexts`, so the staged `vcam/0` instance
was classified as `default_android_service`. The AVD-only live policy therefore
needed only provider add/find and CameraService find access for that fallback
label. A product build instead installs the repository's exact
`hal_camera_service` mapping and does not use the fallback service label.

The first AOSP attempt also exposed an OEM dependency in the module policy:
`vendor_camera_data_file` is absent on this image. VCAM now owns the
`vcam_camera_data_file` type and labels only `/data/vendor/camera/vcam`. The
backend retains scoped write access, while cameraserver and the provider receive
read-only access. The product form compiled successfully into
`vendor_sepolicy.cil` against `android-14.0.0_r23`, including Android's policy
versioning and neverallow checks.

After a reboot cleared the prior live rules, the complete AVD policy file was
applied in one operation. Before topology publication, Camera1 was rejected,
Camera2 characteristics reported `Camera routing topology unavailable`, NDK
enumeration returned zero devices, and no rewrite or open reached a physical
camera. The map generator then published `10 -> target 0`, Camera1 index
`0 -> target 0`, and internal indices `1000 -> 1`, `1001 -> 2`; every generated
file retained the dedicated SELinux label.

The repeated public-API probe reached `probe_compatible` with required, seen and
valid masks all `0x0000000000000f6f`. Six rewrite attempts succeeded with zero
failures. A subsequent public-ID `10` preview opened internal ID `1000`, rewrote
its first capture request, and sustained about 30 fps. The YUV stream reached
570 frames (`avg=127`, `min=0`, `max=255`) while the provider and cameraserver
PIDs remained unchanged. No relevant AVC denial or application fatal exception
was observed.

The live loader used for this disposable-image test is not shipped by VCAM. It
only supplies the same small dynamic rules that APatch/KernelSU would load for a
systemless module; product integration uses the compiled policy above.

After qualification, the test app and provider were stopped, the VINTF and init
fragments, route data, custom libraries and router were removed, every replaced
vendor library was restored from its pre-test copy, and the exact stock cameraserver
(`b40500255b1b9d51ea59d0dce3ac6c00fba81f605226297741d4a95ab849df99`)
was restored. After an Enforcing reboot, `media.camera` was present, only
physical device `10` remained, and no `vcam` service was registered.

## Report-only module isolation

The independent Android 14 capability-probe archive was validated to contain
only module metadata, `skip_mount`, documentation and shell scripts. It has no
partition overlay, native/ELF payload, SELinux rule, Provider, router or
CameraService replacement. Because the AVD has no APatch/KernelSU module
manager, its exact staged `service.sh` lifecycle was invoked from `/data` as
root; this validates runtime behavior but not a specific root manager's install
UI.

Before and after the service run, cameraserver retained PID 453,
`libcameraservice.so` retained SHA-256
`52fa175391f4bc753e5cddd6d541ceff4b4c83dd657aa0cc1e6edbe8deaec751`,
the two stock `internal/0` and `internal/1` AIDL Provider services were
unchanged, and CameraService still exposed only public device `10`. No
`/dev/vcam` or `/data/vendor/camera/vcam` path appeared. The generated schema 6
profile classified the AVD as `probe_required`; the module result remained
`activation_policy=probe_only`, `routing_authorized=false` and
`camera_mutation_performed=false`.

After the probe, the ordinary test APK opened device `10` and reported
`Camera2 single-stream preview 1280x720 running`. An uninstall simulation
removed the probe state while a sentinel under the release configuration path
remained intact. All staged probe files and test state were then removed and
adbd returned to non-root mode. Limiting potentially blocking `lshal`, service
enumeration and CameraService dump queries reduced a subsequent profile run on
this AVD from an unbounded wait to about 0.7 seconds.

## What this proves

The AVD proves that the same-process launcher, stable-AIDL v2 provider,
internal-ID hiding and a global public-to-virtual route work together on a clean
AOSP Android 14 stack under SELinux Enforcing. It covers Camera1, Camera2 and
NDK clients, fail-closed topology publication, product-policy compilation and
real frame delivery through a public ID. It also removes the relocated-
executable, hard-coded public-ID and OEM camera-data-label assumptions from the
portable architecture.

It does not prove OEM compatibility, MetaModule behavior, non-pattern media
sources on the AOSP provider, or third-party application breadth. Automatic
AOSP capability detection is now packaged separately as a fail-closed probe.
The same archive has also completed KernelSU's real x86_64 module installer and
command-line lifecycle on a second disposable AVD; the AVD-specific limitations
and reproducible kernel build are recorded in
[the KernelSU AVD harness](avd-kernelsu.md). The next platform gate is an
independently implemented Android 14 camera stack.
