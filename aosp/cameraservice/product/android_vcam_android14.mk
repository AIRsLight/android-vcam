# Android 14 uses stable camera provider AIDL v2. The legacy HIDL frontend is
# intentionally omitted on AIDL-native products.
PRODUCT_PACKAGES += \
    android.hardware.camera.provider-service-vcam-v2

PRODUCT_VENDOR_PROPERTIES += ro.vendor.vcam.provider.enabled=true
