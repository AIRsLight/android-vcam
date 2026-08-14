# AOSP camera provider frontends

This directory contains transport adapters for the common VCAM frame engine.
They are designed to be included from an AOSP product tree rather than linked
against private OEM camera libraries.

The first supported baseline is Android 12's HIDL 2.4 provider transport. The
service deliberately reports no device until the Device/Session milestone is
enabled, which makes provider registration safe to validate independently.
The AIDL frontend targets Android 13 and newer and will share the same frame
engine and persistent provider configuration.

Provider instances use `vcam/0`; they never replace or reuse the OEM
`legacy/0` or `internal/0` service name. CameraService selects a VCAM session
through its injection/routing adapter while the OEM provider stays registered
for physical fallbacks.

## Product integration

Add the relevant service to a product makefile:

```make
PRODUCT_PACKAGES += android.hardware.camera.provider@2.4-vcam-service
```

The HIDL manifest fragment and init service are attached to the Soong module.
Device policy must also include matching `hal_camera` service and binder rules;
those rules belong in the product sepolicy because public policy differs by
Android release.
