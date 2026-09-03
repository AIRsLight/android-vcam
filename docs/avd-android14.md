# Android 14 AVD qualification

This is a platform/API qualification target, not an OEM device profile.

## Baseline

| Field | Value |
| --- | --- |
| AVD | `vcam_aosp14_api34` |
| System image | `system-images;android-34;default;x86_64` revision 4 |
| Android / SDK | 14 / 34 |
| Build fingerprint | `Android/sdk_phone64_x86_64/emu64x:14/UE1A.230829.036.A1/11228894:userdebug/test-keys` |
| Public camera IDs | `10` |

## Public-client result

The dual-ABI test APK was installed with its camera permission granted and
launched through `io.github.androidvcam.test.RUN_PROTOCOL_PROBE`.

- the x86_64 NDK probe loaded and returned one camera and one characteristics
  record;
- the Java concurrency query returned zero combinations;
- invalid-ID torch off, strength-set, and strength-get requests reached
  CameraService and were rejected without activating a flashlight;
- Camera1 opened and closed back-facing API1 device 0 successfully; and
- Camera2 opened and closed back-facing public device 10 successfully.

No application crash, cameraserver restart, or stuck camera owner was observed.
This validates the ordinary-client self-test and its x86_64 packaging. It does
not validate the in-process Binder router because the stock AVD cameraserver was
not replaced or preloaded.

## Next AVD gate

Build the Android 14 router, launcher, and AIDL provider for the `aosp_x86_64`
product. Deploy them only to this disposable userdebug AVD with a saved stock
cameraserver copy and a next-start stock fallback. First qualify `preflight`,
then pure `passthrough`; do not enable physical or virtual routing during the
same experiment.

The x86_64 compile uses the same pinned AOSP r23 validator:

```text
tools/verify-aosp14-build.sh --aosp-root /path/to/aosp \
  --mode build --product aosp_x86_64
```
