# Localized route master switch dev.39 release

Version `0.5.0-dev.39` adds English and Simplified Chinese Manager resources,
including Android 13+ per-app language metadata. The Routes page now exposes a
persistent master switch that can disable every configured app route without
deleting routes or providers. While disabled, new camera sessions resolve to
the stock physical camera; enabling it restores the saved routing table.

The release remains one auto-detecting root module for the two qualified
profiles: OnePlus 7 Pro Android 12 under APatch and NX769J Android 14 under
KernelSU. Unknown fingerprints continue to fail closed.

| Artifact | SHA-256 |
| --- | --- |
| `android-vcam-manager-v0.5.0-dev.39-debug.apk` | `c686644d9a45b2f9fa63e17e8f515271dfaf38621b9591002cb05c2935d8033a` |
| `android-vcam-camera2-test-v0.5.0-dev.39-debug.apk` | `9d67c9caecb11f44249f82c23aa5cf135963e8705aa541e17be7011042719e40` |
| `android-vcam-module-v0.5.0-dev.39.zip` | `dc9db7d6c0846cae1e6ec6a9c47a4ba7af5e92e8304c84cc6ac6dc5a297173a4` |
