# Android 13 CameraService integration

This directory contains the scoped-routing patch for the exact
`android-13.0.0_r84` `frameworks/av` revision
`95be9bad234d69f4a8ded5ee72b60315b1353098`.

Android 13 keeps both HIDL and stable-AIDL camera-provider transports. The
routing layer remains transport-neutral: public target IDs are resolved to
the private VCAM IDs before provider selection. The selected package identity
is then carried into both `HidlCamera3Device` and `AidlCamera3Device`, allowing
the provider-owned session vendor tag to be attached regardless of transport.

The first Android 13 build baseline continues to use the standalone HIDL 2.4
VCAM provider. This verifies framework compatibility without depending on an
OEM AIDL implementation. A native stable-AIDL VCAM provider can be added later
without changing the routing or scope contract introduced by this patch.

Validate from Linux with:

```bash
tools/verify-aosp13-build.sh \
  --aosp-root /aosp/src/android-13.0.0_r84 \
  --mode check

tools/verify-aosp13-build.sh \
  --aosp-root /aosp/src/android-13.0.0_r84 \
  --mode build \
  --jobs 10
```

Build mode copies this repository into the marked
`vendor/android_vcam_buildcheck` directory, applies the framework patch only
for the duration of the build and restores the pristine `frameworks/av`
checkout on exit. It does not flash or modify a connected Android device.
