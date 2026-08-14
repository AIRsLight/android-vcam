# android-vcam

System-level, app-scoped virtual camera project. The currently installable
adapter is pinned to a OnePlus 7 Pro Android 12 ROM; the repository also
contains the transport-neutral frame core and the first AOSP HIDL Provider
service skeleton. Target applications are not hooked: Camera1, Camera2 and
CameraX keep using the normal framework and CameraService path.

The module injects a small proxy into the original Qualcomm Camera HAL. Every
`(application package, target camera)` pair can route to physical camera 0,
physical camera 1, a static image, built-in color bars, a local video, or an
HTTP/HTTPS/HLS/RTSP source. Applications without a route remain physical.

## Implemented

- Physical cameras `physical-0` and `physical-1` are immutable providers and
  may both be used as sources or routing targets.
- User providers can be added, stopped, started and removed independently.
- A native, root-free manager app separates status, providers and per-app
  camera 0/1 routes into tabs. Source creation includes a live preview,
  source FPS/resolution limits, and independent camera 0/1 framing with a
  fixed viewport, draggable media, pinch zoom and continuous zoom control.
- A module-owned `vcamd` daemon persists configuration independently of the
  manager and exposes only an authenticated, fixed-command local protocol.
- A transport-neutral route resolver is shared by the OEM compatibility proxy
  and the standalone AOSP Camera3 module; unscoped standalone sessions fail
  closed.
- An Android 12 AOSP CameraService patch performs scoped redirection to hidden
  provider IDs 1000/1001 and fails closed for configured but unavailable
  providers.
- Native FFmpeg 4.2.2 integration decodes local, HTTP, HTTP-HLS and RTSP input.
- HTTPS video files are downloaded through the ROM's BoringSSL-enabled curl,
  then decoded in the background.
- A regular root-free Camera2 APK validates two-stream YUV preview.
- Module installation is systemless and pinned to the tested ROM hashes.

## Pinned target

| Property | Required value |
| --- | --- |
| Device / product | `OnePlus7Pro` / `OnePlus7Pro_CH` |
| Android | 12 / API 31 |
| ABI | `arm64-v8a` |
| Build | `P.202303230244` |
| OEM HAL SHA-256 | `dab50dfd0bde9f710c92097442d6451695f7ef82cb9e836b0af2b9369751daa6` |

Do not install the APatch module on another device or ROM build.

## Build

The tested toolchain is under `D:\AndroidSdk`: NDK `27.2.12479018`, CMake
`3.22.1`, Android platform 35 and JDK 17.

```powershell
pwsh -File tools/fetch-platform-deps.ps1
pwsh -File tools/fetch-device-ffmpeg.ps1
pwsh -File tools/package-release.ps1
```

Outputs:

```text
dist/android-vcam-apm-v0.3.2-dev.zip
dist/android-vcam-manager-v0.3.2-dev-debug.apk
dist/android-vcam-camera2-test-v0.3.2-dev-debug.apk
```

The APatch ZIP contains a patched copy of the pinned OEM HAL, the dependency
proxy, the CameraService package-tag patch, native publisher/stream decoder,
controller daemon/scripts and the legacy WebUI. It never contains or modifies the device's
partition-resident files in place. It declares `skip_mount` and performs three
guarded bind mounts, so an APatch metamodule is not required.

## Repository

```text
hal/        Virtual Camera3 implementation
native/     OEM proxy, FFmpeg decoder, publisher, control daemon and NDK build
manager/    Root-free management APK with status, providers and per-app routes
testapp/    Ordinary root-free Camera2 test APK
apmodule/   APatch lifecycle, provider controller and WebUI
tools/      Inspection, patching, build and packaging scripts
tests/      Host-side source/frame tests
docs/       Architecture, frame format and recovery notes
aosp/       AOSP HIDL/AIDL Camera Provider frontends
```

See [architecture](docs/architecture.md), [development and recovery](docs/development.md),
[device support](docs/device-support.md), and [source format](docs/source-format.md).
