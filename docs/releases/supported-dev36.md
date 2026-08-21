# Network-provider edit dev.36 release

Version `0.5.0-dev.36` fixes network-provider edits in the shared backend. The
update path now passes the provider ID when restarting an active source, rather
than calling the restart helper without an ID and returning `provider not found`.

| Artifact | SHA-256 |
| --- | --- |
| `android-vcam-manager-v0.5.0-dev.36-debug.apk` | `ffd31b52d47ad20526402e626c3cb40e167e577f1bd36fb6e6c0ce1abe3bd92b` |
| `android-vcam-camera2-test-v0.5.0-dev.36-debug.apk` | `5a1b771f44b64283851dd3621bbb6bce9d3370352a0fee24a1e91cd0268fc665` |
| `android-vcam-module-v0.5.0-dev.36.zip` | `fbe04b2166ec6489eeeb77830c446686885bdc9d411c184c4813f14b66527ce5` |

OnePlus 7 Pro validation edited the active RTSP provider through the Manager,
changed camera 0 framing from `0.39×` to `5.33×`, and saved it. The Manager
reported `视频源已更新`, the backend persisted `view0=0,5330,288,492`, and the
RTSP provider restarted in the running state. Diagnostic framing changes were
then restored to their original values.
