# AOSP camera provider frontends

This directory contains transport adapters for the common VCAM frame engine.
They are designed to be included from an AOSP product tree rather than linked
against private OEM camera libraries.

The first supported baseline is Android 12's HIDL 2.4 provider transport. It
loads the standalone `camera.vcam` Camera3 module and delegates Device/Session,
FMQ, buffer-cache, fence and gralloc transport to AOSP's
`camera.device@3.4-impl`. The AIDL frontend targets Android 13 and newer and
will share the same frame engine and persistent provider configuration.

Provider instances use `vcam/0`; they never replace or reuse the OEM
`legacy/0` or `internal/0` service name. The two collision-free virtual device
names are `device@3.4/vcam/1000` and `device@3.4/vcam/1001`, corresponding to
back and front replacement targets. CameraService selects a VCAM session
through its routing adapter while the OEM provider stays registered for
physical fallbacks.

The AOSP routing adapter writes the provider-owned BYTE vendor tag
`io.github.androidvcam.clientPackage` into session parameters. The standalone
module resolves that package against the same `routes.tsv` used by the manager.
The OnePlus compatibility adapter continues to use its separate
`com.oplus.packageName` contract.

## Product integration

Add the relevant service to a product makefile:

```make
PRODUCT_PACKAGES += \
    camera.vcam \
    android.hardware.camera.provider@2.4-vcam-service

# Enable only after the CameraService routing adapter and product SELinux
# policy have been integrated and validated.
PRODUCT_VENDOR_PROPERTIES += ro.vendor.vcam.provider.enabled=true
```

The HIDL manifest fragment and init service are attached to the Soong module.
Device policy must also include matching `hal_camera` service and binder rules;
those rules belong in the product sepolicy because public policy differs by
Android release. Without the product property, the service registers but
returns an empty device list. See `sepolicy/README.md` for the minimal product
policy integration.
