# android-vcam

System-level, app-scoped virtual camera prototype for a pinned OnePlus 7 Pro
Android 12 ROM. Target applications are not hooked: Camera1, Camera2 and
CameraX keep using the normal framework and CameraService path.

The module injects a small proxy into the original Qualcomm Camera HAL. Every
`(application package, target camera)` pair can route to physical camera 0,
physical camera 1, a static image, built-in color bars, a local video, or an
HTTP/HTTPS/HLS/RTSP source. Applications without a route remain physical.

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
- A self-contained, statically linked FFmpeg 4.2.2 decoder handles local,
  HTTP, HTTP-HLS and RTSP input without relying on the ROM's reduced protocol
  set. Network preview and provider startup both require a decoded first frame.
- HTTPS video files are downloaded through the ROM's BoringSSL-enabled curl,
  then decoded in the background.
- A regular root-free Camera2 APK validates two-stream YUV preview and accepts
  launch extras for single-stream high-resolution output tests.
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
pwsh -File tools/package-release.ps1
```

Build the pinned static FFmpeg SDK once on Linux (the CI builder uses NDK
r27d), then place its `arm64-v8a` output under
`.reference/ffmpeg-android/arm64-v8a` before running `build-native.ps1`:

```bash
tools/build-ffmpeg-android.sh --ndk-root /path/to/android-ndk-r27d
```

Outputs:

```text
dist/android-vcam-apm-v0.4.0-dev.zip
dist/android-vcam-manager-v0.4.0-dev-debug.apk
dist/android-vcam-camera2-test-v0.4.0-dev-debug.apk
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
```

See [architecture](docs/architecture.md), [development and recovery](docs/development.md),
and [source format](docs/source-format.md).
