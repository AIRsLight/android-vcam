# Delivery milestones

This roadmap turns privacy enforcement, camera contract coverage, and Android
device compatibility into separate release gates. A build is not considered
supported merely because it compiles or boots.

## Product guarantees and non-goals

The project distinguishes three different goals:

1. **API transparency** — Camera1, Camera2, CameraX and NDK clients keep using
   the normal framework path and receive app-visible camera IDs.
2. **Privacy enforcement** — a protected application cannot obtain physical
   camera frames through a covered camera API, including when a virtual source
   or routing component fails.
3. **Fingerprint reduction** — static metadata, capture results and control
   behavior resemble the selected physical target as closely as the virtual
   backend can truthfully support.

Fingerprint reduction is not an undetectability guarantee. An adversarial app
can compare frames with motion sensors, focus, exposure, zoom, flash, scene
changes and sensor-noise behavior. Root, system/vendor-privileged clients and
external capture hardware are outside the ordinary-app privacy boundary.

Two routing modes will remain explicit:

- **Compatibility mode** may use physical providers and may permit a documented
  physical fallback for legacy adapters.
- **Privacy mode** never falls back to a physical camera for a protected
  installation. Unknown identity, unsupported camera ID, missing frame,
  stopped decoder, provider crash and invalid configuration all fail closed.

General-purpose releases must not label compatibility mode as privacy
protection.

## Status legend

| Status | Meaning |
| --- | --- |
| Complete | Implemented and covered by its milestone exit tests |
| Partial | A usable implementation exists but the exit gate is not satisfied |
| Planned | Design is recorded; implementation or qualification has not started |

## M0 — Portable foundation

**Status: Partial**

The existing baseline includes the transport-neutral frame renderer, YUV/RGB
provider format, per-target framing, persistent provider and route state,
root-free manager, OnePlus Android 12 compatibility adapter, AOSP HIDL/AIDL
frontends, and an Android 14 r23 Soong build validator.

Known limitations that remain release blockers:

- the current OnePlus compatibility adapter can fall back to a physical camera
  when package scope or a virtual provider is unavailable;
- AOSP virtual devices derive their static metadata from EmulatedCamera rather
  than the selected physical target;
- routes are keyed by package name instead of installation identity and signer;
- the APatch control socket is reachable from the broad `untrusted_app` domain,
  while daemon authorization checks only package name and peer UID;
- the APatch daemon domain remains permissive;
- CameraService coverage is incomplete for additional camera IDs and advanced
  entry points; Android 14 currently hides internal concurrent-camera sets
  instead of exposing a fully remapped virtual concurrency contract.

M0 is complete only after these limitations are represented by failing or
expected-failure tests, so later milestones can close them without regression.

## M1 — Privacy enforcement boundary

**Status: Planned — blocks every privacy-labelled release**

### Identity and policy

- Store a route against `userId`, package name and signing-certificate digest.
- Resolve the current UID at runtime; never persist a UID as the sole identity
  because it may change or be reused after uninstall.
- Require explicit re-authorization when the signer changes.
- Define deterministic behavior for shared UIDs, SDK sandbox UIDs, isolated
  processes, work profiles and secondary users.
- Use the CameraService-validated caller identity. Client-supplied session
  metadata must never be accepted as authority.
- Record delegation explicitly. Google Play services scanners, system camera
  intents, document-scanner services and companion packages are separate
  callers and are not implicitly covered by the initiating package's route.

### Complete mediation

- Cover every public camera ID discovered on the device, not only `0` and `1`.
- Deny direct access to provider-owned internal IDs.
- Apply policy consistently to Camera1, Camera2, CameraX, NDK, MediaRecorder,
  concurrent sessions, logical/physical cameras, camera extensions, torch,
  external cameras and offline/reprocess entry points when supported.
- Prevent ordinary apps from opening camera device nodes or finding camera HAL
  services directly through product SELinux policy.
- In privacy mode, a configured route that cannot produce a virtual session
  returns a standard disconnected/unavailable error. It must not call the
  physical HAL.

### Control-plane hardening

- Replace package-name-plus-UID manager authentication with signer-bound
  authorization.
- Restrict the control endpoint to a dedicated, signature-bound SELinux app
  domain or an equivalent signature-permission broker; remove broad
  `untrusted_app` socket access.
- Move `vcamd` to an enforcing domain with narrowly audited file, network and
  service-manager permissions.
- Keep provider media and routing data unreadable to ordinary apps.
- Return generic camera errors to clients while retaining detailed diagnostics
  only in protected logs.

### M1 exit gate

For a protected installation, all of the following must produce no physical
camera frame:

- provider stopped, killed or restarted;
- source file removed or malformed;
- RTSP/HTTP source disconnected before and during a session;
- route file absent, partially written or invalid;
- package identity missing, ambiguous or signed by an unapproved certificate;
- direct requests for visible, additional and internal camera IDs;
- Camera1, Camera2, CameraX and NDK opens;
- manager uninstalled, reinstalled, or replaced by the same package name under
  another signing key.

The test records the selected backend for every configure request and fails if
any protected case reaches a physical backend. Root and privileged-client
bypass is documented but is not part of this gate.

## M2 — Mainstream camera contract

**Status: Partial**

This milestone targets barcode scanning, OCR/document capture, social and chat
camera screens, ordinary still capture, video calls, recording and live
streaming before professional-camera features.

### Required stream combinations

| Scenario | Required outputs |
| --- | --- |
| Preview | one `PRIVATE` / implementation-defined surface |
| Barcode/OCR | preview plus `YUV_420_888` analysis |
| Still capture | preview plus JPEG |
| Analysis and still | preview plus YUV plus JPEG |
| Video call | low-latency preview or encoder surface, optionally plus YUV |
| Recording/streaming | preview plus MediaCodec/MediaRecorder private surface |

The baseline size set includes 640x480, 1280x720 and 1920x1080, plus common
4:3 sizes selected from the target camera. 2560x1440, 3840x2160 and larger
still outputs are enabled only when both the frontend and pixel budget have
been validated. The baseline frame-rate set is 15, 24 and 30 fps; 60 fps is a
separate performance qualification.

### Output behavior

- Input-provider resolution remains independent from client-output resolution.
- Every target camera applies its saved rotation, crop, pan and scale while the
  app-visible output buffer keeps the requested dimensions.
- YUV plane strides, chroma steps, crop rectangles and acquire/release fences
  must be correct for CPU ImageReader consumers.
- Preview, analysis, JPEG and encoder surfaces from one request must show the
  same source frame and framing transform.
- Frame numbers and sensor timestamps must be monotonic. Repeated low-FPS source
  frames must not cause timestamp regression or unbounded queue growth.
- JPEG orientation, dimensions, thumbnail and EXIF fields must agree with the
  returned image.
- Front-camera preview mirroring and recorded/image output mirroring follow the
  Android API contract rather than being baked inconsistently into source data.
- Zoom and crop requests either produce a coherent image-space change or are
  omitted from advertised controls.

### M2 exit gate

- The regular test APK passes every required stream combination on front and
  back targets at baseline sizes.
- A CameraX test covers Preview, ImageAnalysis, ImageCapture and VideoCapture.
- A barcode test continuously consumes YUV at 720p and 1080p without leaked or
  stalled buffers.
- A recorder test sustains 30 fps at 720p and 1080p with monotonic timestamps;
  4K and 60 fps results are reported separately.
- Source resolutions below, equal to and above the output resolution pass the
  same crop/scale assertions.
- Unsupported stream combinations fail during support/configuration queries,
  not through a cameraserver or provider crash.

## M3 — Physical metadata profile and advanced specifications

**Status: Planned**

### Target-specific characteristics

For each visible target, CameraService obtains physical metadata `P` and
virtual capabilities `V`, then exposes a compatibility profile `C`:

```text
C = physical identity fields from P
    + stream/control capabilities truthfully implemented by V
    - unsupported standard and vendor capabilities
```

The profile keeps physical-facing identity such as lens facing, sensor
orientation, active/pixel array, physical size, focal lengths and apertures.
It recalculates stream configurations, minimum/stall durations, output-stream
counts, hardware level, request/result/session keys, pipeline depth and
capability flags. Vendor tags are translated by fully qualified name and type
or removed; raw numeric vendor tag IDs are never copied across providers.

The same profile must drive app-visible characteristics, session validation
and virtual-device configuration. Advertising a physical capability that the
selected virtual device rejects is a milestone failure.

### Dynamic capture results

Generate or filter coherent values for sensor timestamp, exposure, sensitivity,
AE/AF/AWB state, focus distance, crop/zoom, frame duration, flash, stabilization
and pipeline depth. Values must agree with accepted CaptureRequests and with
visible frame behavior. Internal routing tags and IDs must not appear in public
metadata or client-facing error messages.

### Advanced capability policy

RAW/DNG, depth, high-speed sessions, input reprocessing, zero-shutter-lag,
Ultra HDR/JPEG-R, HEIC, camera extensions, logical multi-camera and manual
sensor control remain disabled until individually implemented and tested.
Professional camera apps may use the baseline preview/JPEG path, but the
project must not advertise an advanced capability merely to copy a physical
fingerprint.

### M3 exit gate

- A metadata-diff test compares unscoped physical and scoped compatibility
  profiles and explains every intentional difference.
- Every advertised request and result key has a conformance test.
- Characteristics, configured streams and capture results agree for both
  target cameras.
- Fingerprint-reduction tests cover orientation, zoom, focus/exposure state,
  frame timing and public error behavior. Passing this gate reduces obvious
  fingerprints but does not claim adversarial undetectability.

## M4 — Android 10–14 system integration

**Status: In progress**

Each Android generation is pinned to an exact AOSP tag and compiled against
that source. A patch that applies to one release is not reused without its own
build and runtime validation.

| Android | Camera transport target | Required deliverable | Current status |
| --- | --- | --- | --- |
| 10 | HIDL Provider 2.4 / Camera HAL3 | version-pinned CameraService and HIDL frontend build | Planned |
| 11 | HIDL Provider 2.4 plus concurrent-camera APIs | Android 11 patch, ID filtering and concurrency tests | Planned |
| 12 | HIDL Provider and current OEM legacy adapter | AOSP frontend plus pinned OnePlus compatibility adapter | Partial |
| 13 | stable AIDL v1 with HIDL-vendor coexistence | AIDL v1 frontend, mixed-transport discovery and runtime tests | Partial |
| 14 | stable AIDL v2 with OEM variants | AIDL v2 frontend, CameraService integration and product policy | Soong build plus offline OEM ABI/transaction guard, shadow bridge, Parcel observer and isolated rollback transaction complete; live backend/activation pending |

For every version, the build gate covers CameraService, the selected Provider,
VINTF fragments, init service definitions and SELinux policy. The runtime gate
covers enumeration, both target cameras, scoped and unscoped apps, Camera1,
Camera2, CameraX, NDK, provider restart and reboot.

Android versions below 10 and above 14 are out of scope for this roadmap.

## M5 — Device and vendor adapter qualification

**Status: Planned**

System-version support does not imply stock-device support. Tier-1 AOSP/ROM
integration and tier-2 systemless OEM adapters remain separate deliverables.

The read-only device profile must record:

- build fingerprint, security patch, ABI and verified-boot state;
- provider transport, version, instance, executable and VINTF declaration;
- legacy camera module and dependent binary hashes;
- camera IDs, lens facing, logical/physical topology and external cameras;
- gralloc/graphics mapper transport and buffer layouts;
- init service names, linker namespaces, SELinux labels and relevant AVCs;
- root implementation and delivery mechanism, including APatch or KernelSU;
- mechanical camera behavior, including pop-up front cameras and source/target
  combinations that could activate physical hardware.

Adapter families are qualified independently:

1. AOSP/reference ROM integration;
2. Qualcomm CamX and legacy camera-module derivatives;
3. Qualcomm stable-AIDL OEM providers;
4. MediaTek camera stacks;
5. Samsung/Exynos camera stacks.

No family name grants blanket support. A systemless adapter is enabled only for
the exact fingerprints and binary hashes it was built and tested against.
Unknown builds remain read-only and unsupported.

### M5 exit gate per device build

- installation and removal are recoverable without modifying partition files;
- boot, reboot and camera-provider restart succeed under SELinux enforcing;
- the stock camera works with no route and after module disablement;
- front and back targets pass privacy and stream-contract tests;
- unscoped applications remain on their original physical provider;
- scoped physical-source and virtual-source routes behave as configured;
- provider failure obeys the selected compatibility/privacy mode;
- mechanical cameras do not activate unless the selected physical source
  requires them and the user has explicitly allowed that route;
- rollback instructions and source/target hashes are packaged with the adapter.

## M6 — Release certification

**Status: Planned**

A release candidate must publish a matrix containing:

- completed privacy, stream and metadata milestones;
- Android version and exact AOSP tag;
- device fingerprint and adapter hash allowlist;
- supported formats, sizes, frame rates and stream combinations;
- deliberately unsupported professional features;
- Camera CTS/VTS or equivalent focused test results where the build environment
  permits them;
- 30-minute preview/analysis/recording soak results and provider-restart tests;
- known detection surfaces, delegation limitations and privilege exclusions;
- installation, disablement and recovery procedures.

The manager must display the active mode and qualification level. It must not
show “privacy protected” unless M1 passed for the current adapter and device
build, and must not show a size, frame rate or feature as supported unless the
corresponding M2/M3 test passed.
