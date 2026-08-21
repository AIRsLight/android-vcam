# Unified-module dev.32 release

Version `0.5.0-dev.32` is the first release that publishes one root-module ZIP
for every qualified device. Manager and the Camera2 test app remain common
APKs. The `android_vcam` installer requires the active MetaModule supplied by
KernelSU or APatch, then selects a profile by exact build fingerprint, Android
API, architecture and camera ABI. Unknown builds fail closed.

| Artifact | SHA-256 |
| --- | --- |
| `android-vcam-manager-v0.5.0-dev.32-debug.apk` | `bbb20ea051f8786e2136a118a5273ba7b1658816aa20d664b71fe60aafa2fd28` |
| `android-vcam-camera2-test-v0.5.0-dev.32-debug.apk` | `9ca7c1432047bcd4332d1cde5ac15dbf488cdaaab1b14769143a05c4d8492c64` |
| `android-vcam-module-v0.5.0-dev.32.zip` | `658b39aec396e94f51dc1e35950e59d4e709315f443921b57e79b53f8fcc6557` |

Both supported profiles reference the same module artifact:

| Profile | Qualified root path | Published module |
| --- | --- | --- |
| `oneplus7pro-p202303230244` | APatch with an active supported MetaModule | `android-vcam-module-v0.5.0-dev.32.zip` |
| `nx769j-ukq1-20240417` | KernelSU with an active supported MetaModule | `android-vcam-module-v0.5.0-dev.32.zip` |

The archive stores device payloads below `payload/profiles`, outside the
MetaModule mount surface. During installation it retains only the selected
profile and creates one final `/data/adb/modules/android_vcam` tree. Disabled
legacy NX Provider and router modules are retired during migration. Provider,
router and backend transient state is owned by
`/data/adb/android_vcam/runtime`, so legacy uninstall scripts cannot remove a
new one-boot arm request.

## NX769J validation

The frozen module was installed through KernelSU 3.2.4 with the active official
OverlayFS MetaModule 1.3.1. The installer selected
`nx769j-ukq1-20240417`, validated the pinned CameraService ABI, merged the AIDL
Provider and CameraService router under the single `android_vcam` module ID,
and removed the two disabled legacy module identities.

An explicitly armed route boot registered `vcam/0`, enumerated internal cameras
`1000` and `1001`, resumed the saved
`rtsp://192.168.130.171:8554/time` source after network availability, and
reported `physical_route_ready`. The ordinary Camera2 test app opened public
cameras 0 and 1; CameraService recorded successful rewrites to 1000 and 1001,
and the captured RTSP time frame was readable. A following ordinary reboot
returned to five physical cameras, the stock cameraserver path, no VCAM
processes and one disabled `android_vcam` module.

The OnePlus 7 Pro payload passed source, archive and installer-layout checks.
Its new MetaModule delivery path still requires a physical APatch regression
before dev.32 can claim runtime qualification on that device.
