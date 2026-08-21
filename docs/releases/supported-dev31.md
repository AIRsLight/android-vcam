# Qualified dual-device dev.31 release

Version `0.5.0-dev.31` keeps the schema-5 device selection and camera runtime
from dev.30, and replaces the Manager's documentation-heavy home screen with a
compact module-manager layout.

The home page now exposes only service state, provider/route counts, device
qualification and camera test actions. Camera ABI, route protection and error
details are available through the on-demand `详情` dialog. Provider and route
pages no longer carry permanent instructional paragraphs.

| Artifact | SHA-256 |
| --- | --- |
| `android-vcam-manager-v0.5.0-dev.31-debug.apk` | `95cde282e8c16d5ca7296251941efbbc3367fad9481ddc9e0e8518dd8865d195` |
| `android-vcam-camera2-test-v0.5.0-dev.31-debug.apk` | `f5cfb8e5eb9a9aec91433d6327580c8480d2626c1a027a4ad21eba384c337c4a` |
| `android-vcam-oneplus7pro-apm-v0.5.0-dev.31.zip` | `26511e2e25116d18ae5c7d8ca5d16dbff6d3b58c05c18eb0b5d4081706161f83` |
| `android-vcam-aidl-provider-v0.5.0-dev.31.zip` | `c7de8de4923e29399ee9b0eb591be4554d25f7349c432ac92a12a5e8bad123bb` |
| `android-vcam-portable-bootstrap-v0.5.0-dev.31-physical-route.zip` | `24e7245a76ad73472f508e727241ba8fe541e9d57eea60aaee4ee49ba33c08c3` |

NX769J runtime qualification remains recorded in `supported-dev30.md`; dev.31
does not change its provider/router implementation. The compact Manager was
installed and visually checked on the same NX769J device while its one-shot
modules remained disabled and the camera stack stayed stock. OnePlus 7 Pro
runtime regression remains pending until that physical device is connected.
