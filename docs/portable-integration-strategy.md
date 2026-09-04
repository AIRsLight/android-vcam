# Portable system integration strategy

Status: AOSP Android 14 AVD bootstrap, global routing and SELinux Enforcing
qualified; automatic systemless delivery and broader OEM requalification are
pending.

## Decision

For rooted stock Android 10-14 devices, prefer an in-process Binder router in
`cameraserver` plus a standards-based virtual Camera Provider. Do not use live
instruction patching as the default compatibility mechanism.

The router is loaded at process start, after a systemless launcher has entered
the stock `cameraserver` SELinux domain. The launcher loads the device's own
`libcameraservice.so` and performs the small, stable `main_cameraserver` startup
sequence in that same process. It does not execute a relocated cameraserver
binary. The router waits for the real `media.camera` service, verifies that its
Binder object is local to the process, then registers a small forwarding Binder
under the same service name. Unknown transactions are forwarded unchanged to
the original local Binder object.

```text
app Camera1 / Camera2 / CameraX / NDK
                  |
                  v
        media.camera Binder name
                  |
       in-process VCAM router
          |                |
   unchanged request   routed camera ID
          |                |
          +------ stock CameraService ------+
                                             |
                         +-------------------+------------------+
                         |                                      |
                  OEM Camera Provider                 VCAM HIDL Provider
                   physical devices                   internal devices
```

Because the forwarding object and the original `CameraService` Binder live in
the same process, `BBinder::transact()` invokes the original `onTransact()`
directly. The Binder thread retains the real app PID, UID and security context.
This is the critical difference from a separate root proxy process: a separate
process would become the caller and would change permission, AppOps, foreground
UID and permission-filtered metadata behavior.

The initial portable provider target for Android 10-12 remains HIDL Provider
2.4 / Camera Device 3.4. Transport selection is constrained by the device's
target FCM, not just the Android SDK: the NX769J Android 14 device targets FCM 8,
whose framework matrix rejects HIDL camera providers and accepts stable AIDL
v1-v2. Android 13 mixed-transport devices require their own VINTF check. AIDL is
therefore a prerequisite, not an optional extension, for Android 14 / FCM 8.

## Capability modes

The router reports capabilities instead of pretending every device supports
the same policy:

| Mode | Caller selection | Physical camera can open before routing | Intended use |
| --- | --- | --- | --- |
| `scoped-binder` | Binder UID plus validated package/install identity | No | Preferred privacy-capable mode |
| `global-binder` | One route for every ordinary client | No | Fallback when package identity is ambiguous |
| `native-injection` | AOSP global injection state | Yes | Android 13-14 compatibility fallback |
| `oem-adapter` | Adapter-defined | Adapter-defined | Last resort for incompatible camera stacks |
| `physical-only` | None | Yes | Fail-open installation/pass-through state |

Shared UIDs, isolated processes, SDK sandboxes and delegated camera clients
must fail to `global-binder` or `physical-only` according to the selected user
policy. They must not be guessed from a package string supplied by the client.

Internal VCAM IDs are implementation details. If they must be registered as
public Camera Provider devices so ordinary clients can open them through the
stock service, the router must:

- remove them from Camera1 counts and Camera2 status snapshots;
- wrap `ICameraServiceListener` so later status callbacks are filtered;
- reject a direct app request for an internal ID; and
- map a visible target ID to an internal ID only after policy selection.

Static characteristics can initially remain those of the visible physical
target. The internal virtual device must accept the advertised baseline stream
contract, and later metadata blending must keep characteristics, configuration
validation and capture results consistent.

## Why this is the primary systemless path

### Provider-only routing is insufficient

HIDL and AIDL Camera Provider interfaces are the most vendor-neutral boundary,
but calls arrive from `cameraserver`; the original application identity is not
part of the standard provider open contract. A second provider also cannot
transparently take over already-used public camera IDs after the OEM provider
has been enumerated. A provider remains the correct frame transport, but it
needs a framework-side ID selector.

### AOSP camera injection is useful but not general

Android 12 declares `ICameraService.injectCamera`, but its AOSP implementation
is a stub. Android 13 and 14 implement it and can replace an active internal
session with an injection-capable external device. In the inspected releases,
the implementation ignores the `packageName` argument, stores only one internal
and external ID pair, and opens the internal device before replacing its HAL
interface.

It is therefore a valuable Android 13-14 global compatibility fallback, and it
also passes the internal camera's stream configuration and characteristics to
the injection session. It is not a complete Android 10-14 solution, cannot
provide simultaneous independent front/back mappings, and can briefly activate
a physical or pop-up camera before injection.

### A separate `media.camera` proxy changes security semantics

Android's service manager can expose a forwarding service in another process,
but nested Binder calls then originate from that proxy. Connect operations can
carry delegated UID/package fields, while many other CameraService operations
still rely on the kernel Binder caller for permissions, system-camera filtering,
foreground state and AppOps. Reimplementing all of those policies would be both
fragile and unsafe. The proxy is acceptable only when it forwards locally in
the original cameraserver process.

### Live native patching is a last-resort adapter

The existing ELF identity guard, trampoline planner, rollback transaction and
thread-quiescence work remain useful for exact-build OEM research. They should
not be the main Android 10-14 compatibility plan. Locating a symbol is easier
than proving that an arbitrary thread can never execute partially modified code,
and OEM C++ layouts remain exact-build ABIs.

### Source integration remains the strongest tier

For AOSP-derived ROMs that can be rebuilt, a small version-pinned CameraService
route plus the same provider is still the most reliable integration. The Binder
router is the stock-rooted delivery path, not a reason to replace source-level
integration.

## Bootstrap design

Directly setting `LD_PRELOAD` on the init-launched cameraserver is not reliable:
the Android linker removes loader environment variables when `AT_SECURE` is set,
including security transitions. Re-executing a relocated stock cameraserver is
also not a portable fallback: standard AOSP policy has a `neverallow` against
`cameraserver` executing an arbitrary file without a domain transition.

The launcher is therefore executed once at the normal
`/system/bin/cameraserver` path. After init has transitioned it into the stock
domain, it optionally loads the router with `dlopen()`, loads the device's own
`libcameraservice.so`, resolves `CameraService::instantiate()`, and mirrors the
Android 14 `main_cameraserver` Binder/HIDL thread-pool sequence. This keeps the
router and original CameraService local without requesting an OEM SELinux
exception for `execute_no_trans`.

The bootstrap must be qualified independently from the camera protocol:

1. capture and hash the stock executable before any bind mount, solely as a
   recoverable rollback asset;
2. verify that the device `libcameraservice.so` exports the required instantiate
   entry point before activating the launcher;
3. preserve UID, GID, supplementary groups, capabilities and rlimits inherited
   from init;
4. verify the selected linker namespace permits the launcher, router and all
   of their dependencies;
5. install the forwarding service only when the original Binder is local and
   has the exact `android.hardware.ICameraService` descriptor; and
6. use a boot-attempt marker plus the root-manager recovery service so a loader
   failure or early crash disables the module and restores the captured stock
   executable on the recovery boot.

KernelSU and APatch delivery may use different mount helpers, but both should
produce this same runtime contract. Root-manager-specific scripts must not leak
into Binder routing or provider code.

The first Android 14 prototype keeps bootstrap activation separate from module
installation. Its fixed runtime contract is:

- `/system/bin/vcam/cameraserver` is the captured rollback executable and is
  never launched from the `cameraserver` domain;
- `/system/lib64/libvcam_cameraserver_router.so` is the in-process router;
- `/system/etc/android_vcam/bootstrap.mode` selects `stock`, `preflight`, or
  `passthrough`; a missing, empty, oversized, non-regular, or unrecognized value
  fails to `stock`;
- router loading is allowed only after the launcher observes the `cameraserver`
  SELinux domain and a readable router library;
- the launcher atomically creates `/dev/vcam/bootstrap.pending` before loading
  the router. A second launch with that marker still present selects `stock`;
  only a router that has verified the original local CameraService Binder (and,
  for `passthrough`, the replacement registration) clears the marker.

If router loading fails before CameraService starts, the launcher clears the
router mode and starts the device's own CameraService in-process. If the
launcher or CameraService entry point itself is incompatible, the external
root-manager service detects that `media.camera` did not remain stable, disables
the module and restores the captured executable for a recovery boot. Enabling a
new routed attempt requires an explicit control operation that removes the
marker rather than an automatic retry loop.

## Version and vendor adaptation boundary

The expected maintained surface is:

| Layer | Variants for Android 10-14 | Vendor-specific? |
| --- | --- | --- |
| Frame engine, providers, manager protocol | One shared core | No |
| Baseline provider transport | HIDL 2.4 / Device 3.4 builds | Normally no |
| `ICameraService` transaction decoder | One reviewed adapter per Android major | OEM additions are probed |
| Binder listener filtering | One reviewed adapter per Android major | OEM additions are passed through |
| Launcher entry sequence | One reviewed build per Android major | Usually platform-version specific |
| Mount/recovery | APatch and KernelSU capability backends | Root implementation only |
| SELinux/VINTF/init facts | Generated device profile plus small policy recipe | Yes, declarative |
| Live code hook | Exact binary recipe | Yes, fallback only |

Portable policy must not reference a camera-data type supplied by one OEM.
VCAM owns `vcam_camera_data_file` for `/data/vendor/camera/vcam`, grants the
backend scoped write access, and grants cameraserver/provider read-only access.
An integrated product also installs the exact `vcam/0 -> hal_camera_service`
service context. A systemless fragment cannot change servicemanager's already
loaded context table, so the dynamic policy may need a narrowly scoped
`default_android_service` fallback after capability probing.

The transaction router must never infer an unknown Parcel layout. Each adapter
is compiled against a pinned AOSP tag and validates the interface token,
transaction role and expected leading fields. Unknown transactions are passed
through byte-for-byte; a transaction that needs routing but fails validation is
left physical in compatibility mode or rejected in privacy mode.

## Validation gates

Development should proceed in this order:

1. Build Android 10, 11, 12, 13 and 14 pass-through router variants on CI and
   run Parcel corpus tests from the matching AOSP generated proxies.
2. On Android 14, load a no-op agent and prove that the original stock camera
   behavior, service descriptor and caller PID/UID are unchanged.
3. Register a pure pass-through local Binder proxy and test Camera1, Camera2,
   CameraX, NDK, system camera, scanner and video-call clients.
4. Register internal virtual IDs, add complete enumeration/listener filtering,
   and prove that an ordinary test app cannot discover or directly open them.
5. Enable one global back-camera route, then front-camera routing, provider
   failure behavior and reboot recovery.
6. Enable scoped routing only after UID/package/signer and shared/isolated UID
   tests pass.
7. Repeat protocol builds and smoke tests on Android 10-13 before qualifying
   vendor profiles.

Public Camera2 IDs and Camera1 indices must be discovered rather than assumed.
After the virtual provider is registered in pass-through mode, the qualifier
captures CameraService's raw `Device N maps to "ID"` table. It writes one map
from public IDs/legacy aliases to logical target slots and a second map from
Camera2 string IDs to Camera1 integer indices. Physical routing is authorized
only if every selected public target and internal virtual device has an
unambiguous mapping. A missing Camera1 mapping rejects the routed Camera1 open;
it must never silently fall back to the physical device.

This gate is implemented by the portable `camera-map.sh` startup helper. It
keeps Camera2 targets, Camera1 targets and Camera2-ID-to-Camera1-index mappings
in separate files so numeric names cannot collide across API namespaces. The
final `topology.conf` acts as the publication marker. When routes are enabled
but that marker or any map is unreadable, camera-scoped Binder calls fail
closed until validation completes. A generation failure disables the module
for the next boot and enters the existing full-device recovery path.

The first device experiment must stop after gate 2 if the CameraService entry
point is unavailable, the original service Binder is not local, the proxy cannot
be registered safely, or SELinux requires broad permissions. Any of those
results invalidates this primary path and sends the device to native injection
or an exact OEM adapter instead of expanding runtime reflection.

## AOSP evidence used for this decision

- Camera apps call CameraService through Binder, and CameraService is the layer
  that talks to the HAL:
  <https://source.android.com/docs/core/camera>
- Android 13+ supports AIDL camera HALs while retaining HIDL camera HAL support:
  <https://source.android.com/docs/core/camera/camera3>
- Android 12 declares injection but leaves it unimplemented:
  <https://android.googlesource.com/platform/frameworks/av/+/android-12.0.0_r1/services/camera/libcameraservice/CameraService.cpp>
- Android 13 implements the single global injection state:
  <https://android.googlesource.com/platform/frameworks/av/+/android-13.0.0_r1/services/camera/libcameraservice/CameraService.cpp>
- Local `BBinder::transact()` calls `onTransact()` directly:
  <https://android.googlesource.com/platform/frameworks/native/+/android-14.0.0_r23/libs/binder/Binder.cpp>
- Android init supports explicit service environment and SELinux setup, while
  bionic honors `LD_PRELOAD` only when `AT_SECURE` is clear:
  <https://android.googlesource.com/platform/system/core/+/master/init/README.md>
  and
  <https://android.googlesource.com/platform/bionic/+/4b4fb6f43/linker/linker_main.cpp>
