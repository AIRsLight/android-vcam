# Nubia NX769J Android 14 profile

This profile was collected read-only on 2026-08-18. It records one exact
firmware build and does not qualify other NX769J releases.

## Device identity

| Field | Value |
| --- | --- |
| Fingerprint | `nubia/NX769J/NX769J:14/UKQ1.230917.001/20240417.145608:user/release-keys` |
| SDK / release | 34 / Android 14 |
| Platform | Qualcomm `pineapple` / arm64-v8a |
| Root delivery | KernelSU |
| KernelSU userspace | `ksud 3.2.4` with `meta-overlayfs 1.3.1` |
| SELinux | Enforcing |
| Reported physical camera count | 4 |
| Under-screen camera feature | true |

## Camera stack

The OEM service is stable AIDL v2, not the Android 12 legacy-module path.

| Field | Value |
| --- | --- |
| Provider instance | `android.hardware.camera.provider.ICameraProvider/vendor_qti/0` |
| VINTF fragment | `/vendor/etc/vintf/manifest/vendor.qti.camera.provider.xml` |
| Init service | `vendor.camera-provider` |
| Executable | `/vendor/bin/hw/vendor.qti.camera.provider-service_64` |
| Executable SELinux type | `u:object_r:hal_camera_default_exec:s0` |
| Process SELinux domain | `u:r:hal_camera_default:s0` |
| Provider SHA-256 | `ea7b669a7fcb470b6a5e13d818cd650ce897508cc701612d718e4c6487bc9fa0` |
| CameraService SHA-256 | `a26f8ee10002769428871e042c7993e87ad769703897dd75a2fb93a725c64438` |
| Camera client SHA-256 | `1cf518e86a2e5461e585d8dbd7a1dbc93e7ba2bcc95c3e254ebdcc72ee0433c5` |
| Camera client Build ID | `f7cea72167468ee2dfac06e4433d8fa8` |

CameraService reports Camera2 IDs `0,1,2,3,4`; Camera1 exposes `0,1,3`.
IDs `2` and `4` are therefore additional physical access paths even though
they are absent from Camera1. A privacy policy that protects only targets `0`
and `1` is incomplete on this build.

The OEM provider process also owns ZTE transfer and Qualcomm offline, AON and
post-processing interfaces. A standalone virtual provider must use a separate
instance; it must not impersonate or replace `vendor_qti/0`.

### Live restart constraint

`cameraserver` must not be restarted in isolation on this firmware. During a
controlled late-provider diagnostic on 2026-08-20, terminating cameraserver
caused `/vendor/bin/hw/vendor.qti.camera.provider-service_64` to receive its
client death notification and deliberately raise `SIGABRT`. DropBox tombstone
`SYSTEM_TOMBSTONE@1787207852998` records the abort at 14:37:32 in
`camx.provider-impl.so`:

```text
CameraProvider::binderDied(void*)
AIBinder_DeathRecipient::TransferDeathRecipient::binderDied(...)
BpBinder::sendObituary()
```

This is an OEM lifecycle contract rather than a crash in VCAM code. Any
recovery that needs a fresh CameraService must first disable the experimental
module and perform a complete device reboot, allowing init to tear down and
start cameraserver plus the OEM provider in their normal order. Runtime tests
must not use a cameraserver restart to force late-provider discovery.

## ABI assessment

The stock `libcameraservice.so` was compared with the current
`android-14.0.0_r23` VCAM build:

| Metric | OEM UKQ1 library | AOSP r23 VCAM library |
| --- | ---: | ---: |
| File size | 3,132,936 bytes | 3,178,440 bytes |
| Dynamic exported symbols | 3,247 | 3,277 |
| GNU build ID | `747dab9fa491b5af026f143c2c967789` | `3230a4bbcb8e4ffb3505423061c1e632` |

Both libraries declare the same direct `DT_NEEDED` set, but the symbol sets
are not ABI-equivalent: 49 symbols are OEM-only and 79 are r23-only. The
differences include `CameraService` virtual thunks, `Camera3Device`
constructors, rotate-and-crop methods and session-configuration helpers.

The OEM signatures match the corresponding Android 14 initial-release source
shape more closely than r23. An isolated `android-14.0.0_r1` source tree is
therefore the next build baseline; its `frameworks/av` commit is
`402dbe885fd58af75e4c1d7e790fbf4bb22f29f9`. This source match is only a
candidate, not proof that Nubia used an unmodified r1 tree.

## Qualification status

- Read-only discovery: complete.
- AOSP r23 generic provider and CameraService compile: complete on CI.
- Optional undeclared `vcam/0` exact-instance discovery: source build complete;
  service-manager late-registration semantics qualified on device, but not yet
  installed into the OEM CameraService.
- `vcam/0` AIDL v2 contract and 1000/1001 namespace guard: AOSP r23 arm64/arm
  libcameraservice build complete; OEM runtime installation not attempted.
- OEM-compatible CameraService binary: not built.
- OEM Binder transaction extraction: complete for eleven routed operations.
- Offline ARM64 planning and pure pass-through bridge tests: complete.
- Android 14 Parcel classification and cursor-restoring observation: complete
  in offline AOSP r23 host/device builds.
- Shadow-observer pass-through wiring and counter-only telemetry tests:
  complete offline; not bound by the cameraserver agent.
- Transactional patch ordering, target revalidation and rollback fault tests:
  complete with an isolated memory backend only.
- Exact-recipe precompiled ARM64 trampoline: complete offline; embedded in the
  agent text and disassembly-verified, but not bound or installed at runtime.
- Thread-quiescence state machine: complete with an injected backend, including
  target-PC rejection, new-thread stabilization, timeouts and resume failures.
- Android ARM64 signal/futex backend and device test: compiled and
  disassembly-audited; standalone on-device execution is pending. The
  cameraserver agent exposes only read-only signal qualification and never
  installs the handler.
- Read-only maps, target-byte and thread-inventory preflight: complete in
  host/ARM builds; not yet executed inside the device cameraserver.
- Standard Camera2 single-stream physical route: passed for target `0` to
  device `1`; OEM camera and multi-stream routes remain unqualified.
- Portable bootstrap stock-mode test: passed on device, including the OEM camera
  app opening Camera 0 with live preview.
- Read-only in-process Binder preflight: passed on device.
- Same-process Binder pass-through registration: passed on device with dumpsys
  and the OEM camera app; no Parcel routing or frame replacement enabled.
- Runtime visual route test: not started.

## Portable bootstrap delivery

KernelSU 3.x delegates system overlays to a metamodule. Before installing one,
the existing regular module trees were present under `/data/adb/modules` but no
module system files appeared in `/system`. The official `meta-overlayfs 1.3.1`
package (release SHA-256
`279d85a6a35724dfcf8aa1137a9630c7e989e154b94543517c539ee70f9b8811`)
is now installed and its ext4 module image mounts successfully. Regular modules
created before the KernelSU 3.x migration were not copied into that image and
must be reinstalled separately.

The VCAM package uses a dedicated `android_vcam_portable` module ID and ships a
`bootstrap.mode` whose installation default is `stock`. In that mode the
launcher removes loader variables and executes the captured physical
cameraserver. Installation captures and hashes
the exact `/system/bin/cameraserver` before the overlay is activated. At
post-mount, the module verifies launcher and stock hashes plus both
`cameraserver_exec` labels; a failure disables the module and bind-mounts the
captured stock executable over the launcher for the current boot.

Device preflight on 2026-08-19 confirmed:

- stock executable SHA-256
  `4f5d5ff72cf91a4401d9bb5f8a69c06093d82865e59e4d86d2bd950c92bd0082`;
- stock file label `u:object_r:cameraserver_exec:s0` and process domain
  `u:r:cameraserver:s0`;
- the system linker namespace searches `/system/${LIB}`, and its `--list` mode
  resolves every dependency of both ARM64 bootstrap artifacts on the OEM ROM;
- `/system/bin/vcam/cameraserver` and
  `/system/lib64/libvcam_cameraserver_router.so` have no existing path
  collisions.

The first stock-mode boot reached the launcher in the correct cameraserver
domain, but the OEM policy denied `execute_no_trans` from `cameraserver` to the
captured `cameraserver_exec` file. The launcher reported `EACCES`; no router was
loaded. Disabling the module and rebooting restored the exact stock hash and
both camera Binder services. The second package carried the intended permission
using traditional `target:class` syntax, but KernelSU module rules require the
four-part `source target class permission` syntax; the unparsed rule therefore
left the same denial in place. The next package uses
`allow cameraserver cameraserver_exec file execute_no_trans`. Its service stage
also requires one PID and the exact `media.camera: found` result to remain
stable for 10 seconds. On failure it disables the module, binds the captured
stock executable over the launcher and requests a cameraserver restart for
same-boot recovery.

After correcting the policy syntax, stock mode kept one cameraserver PID stable
for more than 10 seconds. The process executable was the captured
`/system/bin/vcam/cameraserver`, its domain remained
`u:r:cameraserver:s0`, CameraService enumerated all five IDs, and the OEM camera
opened Camera 0 with 4080x3060 JPEG plus 1440x1080 preview streams.

Bootstrap control was then moved away from inaccessible vendor camera data:
the read-only mode file is `/system/etc/android_vcam/bootstrap.mode`, while the
per-boot circuit breaker is `/dev/vcam/bootstrap.pending` under a
`cameraserver_tmpfs` directory. In `preflight`, the router DSO appeared in the
stock cameraserver maps, verified the local `android.hardware.ICameraService`
Binder and cleared the pending marker without replacing the registration. The
same OEM camera test passed.

In explicit `passthrough`, the router replaced the `media.camera` registration
with its same-process `BBinder`, verified that ServiceManager returned the new
object, and cleared the marker. `dumpsys media.camera` traversed the proxy,
reported all five devices, and the OEM camera again opened Camera 0 without a
PID change or CameraService error trace. The device was returned to `stock`
mode after qualification; the router DSO is no longer mapped.

An inactive runtime ABI recipe now exists at
`runtime/recipes/nx769j-ukq1-20240417.tsv`. It pins the OEM file identity plus the
code prefixes of `CameraService::onTransact` and
`CameraService::getCameraCharacteristics`. Passing this guard proves only that
the inspected binary is loaded; no Binder interception strategy has been enabled
or installed on the device yet.

The OEM `onTransact` entry begins with `PACIASP`, stack allocation and three
register-save instructions. The offline ARM64 planner can relocate the first 16
bytes without encountering a PC-relative or control-flow instruction and emits a
BTI-compatible reference trampoline. A second, precompiled exact-build
trampoline is now embedded in the agent so a future backend will not require
`mmap(PROT_EXEC)`, `mprotect` or writable executable storage. It is selected only
when the complete recipe matches and currently has no activation caller. The
Binder transaction table has now been confirmed
directly from all eleven relevant OEM `BpCameraService` stubs. The OEM library
does not export the later `remapCameraIds` method, and the observed constants
match the Android 14 initial-release layout. The exact `libcamera_client.so`
identity is included in the runtime allowlist.

The next layer has an AOSP r23 platform implementation that validates the
CameraService Binder token and reads only the leading camera/package fields for
the allowlisted transaction shape. Its tests use real `android::Parcel`
instances and verify that `dataPosition()` is unchanged after successful,
malformed, wrong-interface, unsupported and null observations. The same
observer is now attached read-only to the qualified NX769J same-process proxy.
The exact fingerprint selects the eleven-operation table; an unknown build
still receives byte-for-byte pass-through but no Parcel decoding. Only atomic
aggregate counts are retained, and observed package names, camera IDs, UIDs and
PIDs are discarded when each call returns.

Portable module `0.5.0-dev.5` installed this observer build. Its router SHA-256
is `6e6b0cd4c8960663c4b93b810648c4f3acbc9f9a7a4939c332301050e3571d56`.
Stock mode remained stable after reboot. A temporary `passthrough` run loaded
that exact DSO, cleared the circuit-breaker marker only after registration,
kept cameraserver PID `7525` stable across `dumpsys`, returned all five camera
devices, and cold-started the OEM camera with a live Camera 0 preview. No
CameraService or cameraserver fatal trace was present. The device was then
returned to `stock`; PID `19247` remained stable for 12 seconds, the router was
not mapped, and `media.camera` remained available.

Portable module `0.5.0-dev.6` adds a system `IPermissionController` lookup for
claimed connect-package names and a privacy-safe `/dev/vcam/router.stats`
snapshot. The file contains aggregate counters only; it never records a package
name, UID, PID or camera ID, and the launcher removes it on every cameraserver
start so stock mode cannot expose stale state. On the OEM camera cold-start
test, the qualified proxy saw 79 transactions: 27 recognized and 52 passed
through as non-routed, with zero malformed or unsupported observations. Two
connect calls carried package claims and both matched the package list returned
for the Binder calling UID. Thirteen camera-scoped calls had UID/PID only and
were deliberately not assigned to a package. There were zero rejected claims
and zero unavailable package lookups. Cameraserver PID `14816` remained stable
through `dumpsys`, with no fatal trace. After returning to stock, PID `18614`
remained stable for 12 seconds, the router was not mapped, and both the stats
file and pending marker were absent.

Portable module `0.5.0-dev.7` qualifies the read-only route decision layer. A
temporary runtime table mapped `com.android.camera` camera 0 to an enabled test
provider and used the reserved `*` package for a camera 1 global provider. The
OEM camera cold-start produced one exact-package candidate, one global
candidate, and seven physical-camera decisions. The proxy still forwarded all
Parcels unchanged, so the OEM camera displayed a live physical preview. There
were zero rejected transactions, unavailable providers, package mismatches or
fatal traces, and cameraserver PID `16080` remained stable through `dumpsys`.
The temporary route/provider files were removed. After restoring stock, PID
`18588` remained stable for 12 seconds; the router was not mapped and the stats,
routes, providers and pending paths were all absent.

Portable module `0.5.0-dev.8` resolves UID-only camera calls to an exact package
only when `IPermissionController` returns a single package for that UID. With
the same temporary rules, the OEM camera produced twelve UID-only observations:
nine had one package and three were ambiguous shared-UID cases. The exact-route
candidate count increased to four while ambiguous callers were not guessed;
one global candidate and eight physical decisions remained. There were zero
unavailable lookups, rejected claims, malformed transactions, unavailable
providers or fatal traces, and PID `7599` stayed stable through `dumpsys`. The
test files were removed and stock PID `16974` then remained stable for 12
seconds with no mapped router or runtime artifacts.

Portable module `0.5.0-dev.9` adds an explicitly gated `physical-route` mode
and a same-width Parcel copier for existing one-digit physical IDs. Host tests
proved that a Camera2 connect Parcel containing a Binder callback can be copied
and changed from `0` to `1` without altering the source bytes, object table,
cursor, package or trailing payload; a variable-width `0` to `1000` request is
rejected. On device, the `com.android.camera` target-0 to physical-1 experiment
reported four rewrite attempts, four successes and zero rewrite failures while
cameraserver PID `16131` remained stable. However, the Nubia camera activity
then exited to the launcher instead of keeping a preview. Returning to stock
immediately restored its live target-0 preview, so this is not qualified as an
end-to-end physical route. It indicates an OEM application assumption about
the selected public ID/camera role even though the Binder and CameraService
layers accepted the rewritten transactions. After ADB PackageInstaller service
was restored, the regular `io.github.androidvcam.test` Camera2 application was
installed and routed from target `0` to physical `1`. The observer reported two
rewrite attempts, two successes and zero failures, with a stable cameraserver
PID. The application nevertheless crashed when starting its repeating preview
request: CameraService returned `submitRequestList:337: Invalid camera request
settings` (`ServiceSpecificException`, code 3). This proves that rewriting the
connect ID alone is insufficient: the application selected stream dimensions
and request state using target-0 characteristics, while the opened device was
physical-1. Physical routing therefore remains experimental until
characteristics, stream combinations and request metadata are kept consistent
with the routed source. The device was returned to stock, all temporary runtime
files were removed, and PID `27660` remained stable for 12 seconds.

Portable module `0.5.0-dev.10` closes the first Camera2 request-consistency gap
without linking the OEM `libcamera_client` ABI. A raw Binder device-user proxy
wraps only the successful `ICameraDeviceUser` returned by the rewritten
`connectDevice` call. For the exact Android 14 `submitRequest` and
`submitRequestList` Parcel layouts it copies the complete request, preserves
the Binder object table and changes the logical settings ID from public `0` to
opened device `1`; unknown or malformed layouts remain byte-for-byte
pass-through. The AOSP r23 host test passed for single and batched requests,
source immutability, Binder-object preservation and unexpected-ID fallback.

The same regular `io.github.androidvcam.test` application then completed a
true routed Camera2 preview at 1280x720. The activity remained foreground with
a live image, cameraserver PID `17036` stayed stable, and aggregate counters
reported two connect rewrite attempts, two successes, zero failures, one
device-user wrapper, one rewritten request batch, one rewritten request and
zero skipped batches. This qualifies the standard single-stream route on this
exact firmware; it does not yet qualify Nubia's OEM camera, multi-stream
sessions, explicit physical-camera settings or arbitrary third-party clients.
After the test, the route and runtime telemetry were removed, stock mode was
restored, and cameraserver PID `17600` remained stable for 12 seconds with
`media.camera` available.

Manual provider probe `0.5.0-dev.14` qualified a safer late-discovery primitive
without modifying the stock CameraService. With `vcam/0` absent, the Android 14
NDK service-manager client reported `declared=false` and registered for the
exact full service name. Starting the provider afterward delivered the expected
registration callback. The direct client then enumerated internal IDs 1000 and
1001, configured a 640x480 YUV stream on each, and received one frame from each
with luma range `[0,255]`. After stopping the probe, `vcam/0` was absent again;
the OEM provider PID `1556` and cameraserver PID `2125` remained present. This
qualifies the Binder service notification mechanism, not CameraService runtime
enumeration: the device was still running its unpatched OEM CameraService.

Do not mount the r23 `libcameraservice.so` over the stock library. The likely
failure modes are a cameraserver dynamic-link failure, virtual-call ABI
mismatch or an OEM camera feature crash. Any future runtime activation also
requires a recoverable KernelSU delivery package, exact fingerprint/hash
allowlist, a reviewed live-process backend, and on-device thread/cache/rollback
qualification. Read-only observation, pass-through and the narrowly scoped
standard Camera2 single-stream route are qualified on this fingerprint;
general transaction mutation and visual routing are not.

An additional HIDL registration diagnostic proved that an undeclared
`android.hardware.camera.provider@2.4::ICameraProvider/vcam/0` can be added to
hwservicemanager with an explicitly gated test override, but stock
CameraService cannot acquire that instance because its own libhidl transport
lookup still requires a VINTF declaration. The provider was stopped and
`ro.debuggable` was restored to `0`. A follow-up cameraserver restart used to
test pre-existing discovery triggered the OEM provider abort documented above;
that restart method is now prohibited on this profile. The persistent HIDL
VINTF experiment remains disabled and must not be installed.

An offline VINTF gate now makes the transport decision explicit. A complete
116-file system/vendor/product/system_ext/odm snapshot plus
`/apex/apex-info-list.xml` was checked with the host `checkvintf` built from
`android-14.0.0_r23`. The unmodified device reports `COMPATIBLE`. Adding the
HIDL 2.4 `vcam/0` fragment reports `INCOMPATIBLE` because the active
`manifest_pineapple.xml` targets FCM 8 and HIDL camera provider is deprecated at
that level. Adding the stable-AIDL v2 `vcam/0` fragment instead reports
`COMPATIBLE`, matching the OEM `vendor_qti/0` AIDL v2 provider. The HIDL module
installer now rejects target FCM 8, and Android 14 work continues with the
one-shot AIDL provider module. No persistent provider module was installed
during this validation.

The stock boot log also confirms the required ordering window: KernelSU enters
post-fs-data at approximately `05:12:39.252`, system `servicemanager` reloads
the device VINTF manifest at `05:12:39.716`, and cameraserver starts at about
`05:12:40.873`. The AIDL one-shot bootstrap runs in the background, waits for
the mounted fragment, and retries stable registration within this interval;
it does not block MetaModule's post-fs-data work.

The first one-shot package (`0.5.0-dev.17`) exercised recovery rather than
provider discovery. MetaModule preserved `vendor_configs_file` on the XML but
created the overlaid `/vendor/etc/vintf/manifest` directory as `vendor_file`.
System servicemanager was denied directory traversal, reported a VINTF parse
error (`Cannot open /vendor/etc/vintf/manifest/: Permission denied`) and refused
the otherwise valid `vcam/0` declaration. The bootstrap wrote `disable` and
requested `reboot,vcam-aidl-recovery` at roughly 5.24 seconds. The following
stock boot completed with `media.camera`, cameraserver and the OEM provider all
healthy and `vcam/0` absent. dev.18 fixes delivery by labeling the entire
`vendor/etc/vintf/manifest` directory chain as `vendor_configs_file`; it does
not add a broad servicemanager permission for `vendor_file`.
