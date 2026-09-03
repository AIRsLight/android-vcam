# android-vcam

System-level virtual camera research for Android 10-14. Target applications are
not hooked: Camera1, Camera2 and CameraX continue through the normal framework
and CameraService path. The repository contains a transport-neutral frame
engine, a root-free manager, HIDL/AIDL virtual Camera Providers, exact-device
compatibility adapters and version-pinned AOSP CameraService integrations.

The currently qualified app-scoped delivery is pinned to a OnePlus 7 Pro
Android 12 ROM and an NX769J Android 14 UKQ1 build. A route can select physical
camera 0/1, a static image, built-in color bars, a local video, or an
HTTP/HTTPS/HLS/RTSP source. Unmatched applications remain on the physical
camera. OnePlus uses a guarded Qualcomm Camera HAL proxy; NX769J uses a
same-process CameraService Binder router and a standalone stable-AIDL v2
Provider.

## Project status

| Area | Status |
| --- | --- |
| OnePlus 7 Pro Android 12 scoped routing | Qualified on one exact firmware |
| NX769J Android 14 scoped public 0/1 routing | Qualified on one exact firmware |
| Root-free manager and manager-independent backend | Implemented |
| Image, local video, HTTP/HTTPS/HLS and RTSP sources | Implemented |
| Unified APatch/KernelSU module with automatic exact-profile selection | Implemented |
| Android 12 AOSP HIDL source integration | Build-validated, runtime qualification incomplete |
| Android 13 AOSP HIDL/AIDL coexistence | Partial build integration |
| Android 14 AOSP stable-AIDL v2 integration | Build-validated; provider path qualified on NX769J |
| Manufacturer-neutral global replacement mode | Planned; not yet enabled in release builds |
| General Android 10 and Android 11 integration | Planned |

This is pre-release system software. Release modules accept only qualified
fingerprints and camera-library identities and fail closed on unknown builds.

## Implemented

- Physical cameras `physical-0` and `physical-1` are immutable providers and
  may both be used as sources or routing targets.
- User providers can be added, edited, stopped, started and removed independently.
- Source decoding supports up to 4096x3072 with a bounded pixel-rate budget:
  4K up to 15 fps and 12.6 MP up to 9 fps. Video and newly imported images use
  planar YUV420 frames while legacy RGB providers remain compatible. Camera
  output uses the same pixel-rate budget, so a lower-resolution source cannot
  accidentally drive an application-requested 4K surface at 30 or 60 fps.
- A native, root-free manager app separates status, providers and per-app
  camera 0/1 routes into tabs. Source creation includes a live preview,
  source FPS/resolution limits, and independent camera 0/1 framing with a
  fixed viewport, draggable media, pinch zoom and continuous zoom control.
  Slow previews show in-form progress and preserve all entered values on failure.
  Existing virtual sources expose their currently published frame through a
  bounded backend thumbnail, with an in-place refresh action.
- The route tab lists only configured packages. New routes can select only
  unconfigured installed apps; uninstalling an app leaves its package route marked
  temporarily unavailable, and reinstalling the same package restores it automatically.
- A module-owned `vcamd` daemon persists configuration independently of the
  manager and exposes only an authenticated, fixed-command local protocol.
  The NX769J AIDL package reuses this backend and includes the static
  media decoder, publisher, bounded late-network retry and provider autostart lifecycle.
- The root-free Manager recognizes both exact qualified builds locally:
  OnePlus 7 Pro Android 12 and NX769J Android 14. It cross-checks the backend
  device profile and Camera ABI hashes when active,
  reports router/provider/rollback state, and launches the ordinary test APK
  against public cameras 0 or 1. Unknown builds remain configuration-only.
- A transport-neutral route resolver is shared by the OEM compatibility proxy
  and the standalone AOSP Camera3 module; unscoped standalone sessions fail
  closed.
- An Android 12 AOSP CameraService patch performs scoped redirection to hidden
  provider IDs 1000/1001 and fails closed for configured but unavailable
  providers.
- Provider intent is persisted separately from runtime state, allowing network
  sources that start before Wi-Fi routing is ready to retry after boot completes.
- A self-contained, statically linked FFmpeg 4.2.2 decoder handles local,
  HTTP, HTTP-HLS and RTSP input without relying on the ROM's reduced protocol
  set. Network preview and provider startup both require a decoded first frame.
- HTTPS video files are downloaded through the ROM's BoringSSL-enabled curl,
  then decoded in the background.
- A regular root-free Camera2 APK validates two-stream YUV preview and accepts
  launch extras for single-stream high-resolution output tests. Explicit
  `OPEN_CAMERA_1000`/`OPEN_CAMERA_1001` actions provide an OEM-independent way
  to select internal qualification devices from ADB.
- Module installation is systemless and pinned to the tested ROM hashes.
- Unknown Android 14 builds can select the AOSP initial-release CameraService
  transaction template for aggregate read-only pass-through observation. This
  is only a probe candidate: actual routing remains restricted to qualified
  device recipes and rejects an unqualified protocol before Binder takeover.
- The pass-through observer publishes per-transaction protocol evidence as
  stable bit masks and a `pending`, `rejected`, or `probe_compatible` verdict.
  The verdict contains no app, UID, PID, package, or camera-ID data and never
  authorizes routing by itself.

## Qualified targets

### Nubia NX769J Android 14

| Property | Required value |
| --- | --- |
| Device / product | `NX769J` / `NX769J` |
| Android | 14 / API 34 |
| Build | `UKQ1.230917.001/20240417.145608` |
| CameraService SHA-256 | `a26f8ee10002769428871e042c7993e87ad769703897dd75a2fb93a725c64438` |
| libcamera_client SHA-256 | `1cf518e86a2e5461e585d8dbd7a1dbc93e7ba2bcc95c3e254ebdcc72ee0433c5` |
| Root delivery | KernelSU + OverlayFS MetaModule |

This profile supports per-app public 0/1 routing to hidden internal 1000/1001,
local and RTSP sources, listener filtering, request repair, delayed-Wi-Fi
recovery and automatic next-boot rollback. Exact details and recovery evidence
are in [the device profile](docs/device-profiles/nx769j-android14.md).

### OnePlus 7 Pro Android 12

| Property | Required value |
| --- | --- |
| Device / product | `OnePlus7Pro` / `OnePlus7Pro_CH` |
| Android | 12 / API 31 |
| ABI | `arm64-v8a` |
| Build | `P.202303230244` |
| OEM HAL SHA-256 | `dab50dfd0bde9f710c92097442d6451695f7ef82cb9e836b0af2b9369751daa6` |
| Root delivery | APatch + active supported MetaModule |

Do not install either pinned delivery on another device or ROM build. Unknown
fingerprints and camera-library hashes must fail closed.

## Roadmap

### 1. Manufacturer-neutral global mode

Add a compatibility tier that replaces every ordinary application's public
camera targets without identifying the caller. The OEM provider remains
registered, while a same-process `media.camera` Binder router maps public IDs
to collision-free virtual Provider IDs. This removes package lookup, shared-UID
ambiguity, per-app route state and OEM session-package tags from the generic
path. Physical-camera sources are excluded from the first global-mode contract
to avoid recursive capture.

Compatibility is selected by Android protocol family and live capabilities,
not by manufacturer or model. Device-side probing will not disassemble system
libraries. Android 10-14 Binder/Parcel templates are generated offline from
pinned AOSP tags; runtime qualification uses service descriptors, HIDL/AIDL
version/hash queries, VINTF, Camera2 tests and staged pass-through observation.
An unknown or ambiguous interface remains stock.

Activation is staged and reversible:

1. validate the stock cameraserver launcher, linker and SELinux environment;
2. load the router in read-only/pass-through mode;
3. run deterministic Camera1/Camera2 tests against the physical and virtual
   Provider paths;
4. enable one-shot global mapping with automatic next-boot rollback;
5. persist activation only after the health gate succeeds.

Results are classified as `certified`, `probe-compatible` or `unsupported`.
Exact hashes remain certification and OTA-invalidation keys, not the sole
generic compatibility rule.

### 2. Android 10-14 protocol matrix

- Android 10-12: HIDL Provider 2.4 / Camera Device 3.4 and version-pinned
  CameraService protocol templates.
- Android 13: runtime HIDL/stable-AIDL transport selection and mixed-provider
  discovery tests.
- Android 14: stable-AIDL v2, initial-release and QPR protocol templates, plus
  broader OEM validation beyond NX769J.

AVD and AOSP userdebug images provide platform-version gates. They verify the
framework, Provider, manager and source pipeline but do not replace real-device
qualification for OEM SELinux, linker, gralloc, lifecycle and camera metadata.

### 3. Specification and privacy completion

- Derive each virtual camera contract from the selected public target's
  standard metadata and the Provider's actual capabilities.
- Filter unsupported RAW, depth, high-speed, HDR, reprocess, logical-camera and
  vendor-tag capabilities instead of advertising an unimplementable superset.
- Complete listener, concurrency, Camera1, Camera2, CameraX and delegated-system-
  camera coverage for the global boundary.
- Keep unknown camera-scoped transactions and unsupported Provider states
  fail-closed in privacy-labelled modes.
- Retain exact-device app-scoped routing as an enhanced capability after a
  device profile is explicitly qualified.

## Build

The tested toolchain is under `D:\AndroidSdk`: NDK `27.2.12479018`, CMake
`3.22.1`, Android platform 35 and JDK 17.

```powershell
pwsh -File tools/fetch-platform-deps.ps1
pwsh -File tools/package-supported-release.ps1
```

Build the pinned static FFmpeg SDK once on Linux (the CI builder uses NDK
r27d), then place its `arm64-v8a` output under
`.reference/ffmpeg-android/arm64-v8a` before running `build-native.ps1`:

```bash
tools/build-ffmpeg-android.sh --ndk-root /path/to/android-ndk-r27d
```

Outputs:

```text
dist/android-vcam-manager-v0.5.0-dev.39-debug.apk
dist/android-vcam-camera2-test-v0.5.0-dev.39-debug.apk
dist/android-vcam-module-v0.5.0-dev.39.zip
dist/android-vcam-supported-v0.5.0-dev.39.json
```

The release contains one `android_vcam` root module. Its installer requires the
active MetaModule recommended by KernelSU or APatch, validates the exact device
fingerprint and camera ABI, then installs only the matching OnePlus 7 Pro or
NX769J runtime profile. Unknown builds fail closed. Device-specific packages
can still be produced for engineering diagnostics, but are not release files:

```powershell
pwsh -File tools/package-aosp14-aidl-provider.ps1
pwsh -File tools/package-portable-bootstrap.ps1 -BootstrapMode physical-route
pwsh -File tools/build-manager.ps1
pwsh -File tools/build-testapp.ps1
```

The current single-module artifact hashes and installation map are listed in
[the dev.39 release snapshot](docs/releases/supported-dev39.md). The previous
read-path performance release is preserved in
[the dev.38 release snapshot](docs/releases/supported-dev38.md). The first
single-module qualification remains documented in
[the dev.32 release snapshot](docs/releases/supported-dev32.md). The earlier
multi-module packaging remains documented in
[the dev.31 release snapshot](docs/releases/supported-dev31.md). The original
NX769J qualification boundary remains preserved in
[the dev.29 integration snapshot](docs/releases/nx769j-dev29.md).

The unified ZIP contains both qualified payload profiles before installation.
The installer removes the unused profile and leaves one module tree. System
files are mounted by the active SU-manager MetaModule; VCAM does not implement
or bundle its own mount engine and never writes partition-resident files.

## Repository

```text
hal/        Virtual Camera3 implementation
native/     OEM proxy, FFmpeg decoder, publisher, control daemon and NDK build
manager/    Root-free management APK with status, providers and per-app routes
testapp/    Ordinary root-free Camera2 test APK
apmodule/   APatch lifecycle, provider controller and WebUI
unified-module/  Single published root-module installer and lifecycle dispatcher
tools/      Inspection, patching, build and packaging scripts
tests/      Host-side source/frame tests
docs/       Architecture, frame format and recovery notes
aosp/       AOSP HIDL/AIDL Camera Provider frontends
```

See [architecture](docs/architecture.md), [delivery milestones](docs/milestones.md),
[development and recovery](docs/development.md), [device support](docs/device-support.md),
and [source format](docs/source-format.md).
