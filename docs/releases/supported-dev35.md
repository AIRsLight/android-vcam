# OnePlus auxiliary-camera passthrough dev.35 release

Version `0.5.0-dev.35` keeps one auto-selected `android_vcam` root module and
fixes two OnePlus 7 Pro regressions. Unconfigured OEM auxiliary camera IDs now
pass through to their requested physical camera instead of being interpreted as
virtual providers. The APatch service also recovers an exact device profile from
the build fingerprint if MetaModule temporarily exposes an empty `profile.id`
during boot, avoiding a false module-disable marker.

| Artifact | SHA-256 |
| --- | --- |
| `android-vcam-manager-v0.5.0-dev.35-debug.apk` | `c9ec8692d22b55f8d45c705bf939a6ed36a819865f114425198da41e3ed9cfa4` |
| `android-vcam-camera2-test-v0.5.0-dev.35-debug.apk` | `358f9fca04bdb3ceff0a50cb65672a81485797dd65df6b46afa1fa0a42f15329` |
| `android-vcam-module-v0.5.0-dev.35.zip` | `46ba658122f7444c5cdf247d7b1a13ace5a14464c10e43a45a7cbc0a55b084ec` |

OnePlus 7 Pro validation used Android 12, APatch 11224 and OverlayFS MetaModule
1.3.1. It covered installation and two consecutive ordinary boots, with no
module `disable` marker. Both boots reported six camera devices, four providers,
eight routes, an active mount and the qualified
`oneplus7pro-p202303230244` router profile.

The stock OnePlus camera opened OEM camera ID 5 and retained the physical
preview. The Camera2 test app opened camera ID 0 and received the configured
RTSP source at about 30 fps with readable timestamp frames. Existing providers
and per-app routes survived the upgrade and both reboots.

The NX769J payload and exact-fingerprint selection remain in the same ZIP. Its
dev.35 build passed host regression and packaging validation; a dev.35 device
regression remains pending because the NX769J was not connected for this test.
