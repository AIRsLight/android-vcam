# Provider-state UI dev.37 release

Version `0.5.0-dev.37` reconciles successful provider start, stop, and removal
actions in the Manager immediately. The provider page no longer waits for a
slow backend refresh or a page switch before showing the new state; a forced
backend refresh still follows to confirm the authoritative state.

| Artifact | SHA-256 |
| --- | --- |
| `android-vcam-manager-v0.5.0-dev.37-debug.apk` | `5fab49f0315f037042c33855db9caa760eb662d7aafa4f44e1fd38b3d91535c8` |
| `android-vcam-camera2-test-v0.5.0-dev.37-debug.apk` | `81981c9b8889a7b9e8b1f492b302a804166be60a6fd56eadfbdb673a9474c47f` |
| `android-vcam-module-v0.5.0-dev.37.zip` | `469178946513827c993067fd1a6360af4390c0be7b80f21b87fd1fe2eec5deb4` |

OnePlus 7 Pro GUI validation covered pattern, image, local video, RTSP, HTTP
transport stream, and HLS providers. Provider stop/start state changed in place
without navigating away. Temporary validation providers and media were removed;
the existing providers and eight routes were preserved.
