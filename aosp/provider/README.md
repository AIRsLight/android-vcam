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

The Android 12 routing adapter and its exact AOSP patch are under
`aosp/cameraservice/android-12`. Internal IDs are removed from public counts,
listener snapshots and status callbacks; direct app opens of those IDs are
rejected.

The AOSP routing adapter writes the provider-owned BYTE vendor tag
`io.github.androidvcam.clientPackage` into session parameters. The standalone
module resolves that package against the same `routes.tsv` used by the manager.
The OnePlus compatibility adapter continues to use its separate
`com.oplus.packageName` contract.

On Android 14, the AIDL v2 frontend installs the three upstream
EmulatedCamera configuration files explicitly and extends the back/front
static stream tables to 2560x1440, 3840x2160 and 4096x3072 for YUV,
implementation-defined and JPEG outputs. The frame engine renders directly
into the requested output buffer, so the source image does not have to match
the advertised sensor size. Output cadence is bounded by both the configured
source frame rate and a 1920x1080@60 pixel-rate budget; high-resolution streams
therefore reduce frame rate instead of allocating an unbounded workload.

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

Include `aosp/cameraservice/product/android_vcam.mk` instead of duplicating the
package/property block when integrating the complete Android 12 path.

The HIDL manifest fragment and init service are attached to the Soong module.
Device policy must also include matching `hal_camera` service and binder rules;
those rules belong in the product sepolicy because public policy differs by
Android release. Without the product property, the service registers but
returns an empty device list. See `sepolicy/README.md` for the minimal product
policy integration.
