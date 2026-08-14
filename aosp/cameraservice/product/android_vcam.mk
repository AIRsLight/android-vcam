# Include from the target product makefile after placing this repository under
# the AOSP source tree.
PRODUCT_PACKAGES += \
    camera.vcam \
    android.hardware.camera.provider@2.4-vcam-service

# Do not enable this property until the CameraService patch and product policy
# have both been built and validated.
PRODUCT_VENDOR_PROPERTIES += ro.vendor.vcam.provider.enabled=true
