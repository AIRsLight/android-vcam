# Qualified dual-device dev.30 release

Version `0.5.0-dev.30` is the first mainline release with one automatic profile
protocol shared by both qualified targets. Manager and the Camera2 test app are
common APKs; camera modules remain device-specific.

| Profile | Required modules |
| --- | --- |
| `oneplus7pro-p202303230244` | `android-vcam-oneplus7pro-apm-v0.5.0-dev.30.zip` |
| `nx769j-ukq1-20240417` | `android-vcam-aidl-provider-v0.5.0-dev.30.zip`, then `android-vcam-portable-bootstrap-v0.5.0-dev.30-physical-route.zip` |

`tools/package-supported-release.ps1` creates the two common APKs, all three
device modules and `android-vcam-supported-v0.5.0-dev.30.json`. The JSON file
records exact fingerprints, module selection and SHA-256 hashes.

| Artifact | SHA-256 |
| --- | --- |
| `android-vcam-manager-v0.5.0-dev.30-debug.apk` | `4ca07ee4d5817b91b0f3c2b5f553c4b88de2b0bbdac8b3a1784b4b3ad88cb425` |
| `android-vcam-camera2-test-v0.5.0-dev.30-debug.apk` | `586c79f9955334367bda507fd71d444e5f89983332711d3fb9f0913037df0e1e` |
| `android-vcam-oneplus7pro-apm-v0.5.0-dev.30.zip` | `13c4c3e58fd10c427cd641c7f72f4192f2abf6f112307286e4fab375b1db005e` |
| `android-vcam-aidl-provider-v0.5.0-dev.30.zip` | `8413dcce2ebc626d4ed4ee9f99235122c2c7ab50a0093d110ab12ee41a2299c8` |
| `android-vcam-portable-bootstrap-v0.5.0-dev.30-physical-route.zip` | `24ac5260b8224dc4b3c8c7494c93e0f6900a203bcc424bfb337e2ec0d4d53460` |

Selection is fail-closed at three boundaries:

1. The installer validates the exact build and camera ABI before accepting a module.
2. The backend schema-5 probe reports a qualified profile only when the live
   stock ABI or the module's own mounted payload matches.
3. Manager requires its root-free fingerprint result and the backend profile
   result to agree before displaying an ABI-verified state.

The NX769J dev.29 qualification evidence remains valid and is preserved in
`nx769j-dev29.md`. dev.30 adds dual-device discovery and strengthens the
NX769J router installer guard; it does not broaden either firmware boundary.
