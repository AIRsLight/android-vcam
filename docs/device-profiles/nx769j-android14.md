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
- Runtime route test: not started.
- Portable bootstrap stock-mode test: package built; installation pending.
- Runtime routing installation on this fingerprint: blocked until stock-mode
  bootstrap and read-only preflight pass on device.

## Portable bootstrap delivery

KernelSU 3.x delegates system overlays to a metamodule. Before installing one,
the existing regular module trees were present under `/data/adb/modules` but no
module system files appeared in `/system`. The official `meta-overlayfs 1.3.1`
package (release SHA-256
`279d85a6a35724dfcf8aa1137a9630c7e989e154b94543517c539ee70f9b8811`)
is now installed and its ext4 module image mounts successfully. Regular modules
created before the KernelSU 3.x migration were not copied into that image and
must be reinstalled separately.

The first VCAM package uses a dedicated `android_vcam_portable` module ID and
ships no `bootstrap.mode`, so the launcher always removes loader variables and
executes the captured physical cameraserver. Installation captures and hashes
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

The next layer now has an AOSP r23 platform implementation that validates the
CameraService Binder token and reads only the leading camera/package fields for
the allowlisted transaction shape. Its tests use real `android::Parcel`
instances and verify that `dataPosition()` is unchanged after successful,
malformed, wrong-interface, unsupported and null observations. This observer
has a tested optional bridge adapter, but the cameraserver agent does not bind
it and nothing has been installed on the device.

Do not mount the r23 `libcameraservice.so` over the stock library. The likely
failure modes are a cameraserver dynamic-link failure, virtual-call ABI
mismatch or an OEM camera feature crash. Any future runtime activation also
requires a recoverable KernelSU delivery package, exact fingerprint/hash
allowlist, a reviewed live-process backend, and on-device thread/cache/rollback
qualification. The isolated transaction test is not that qualification.
