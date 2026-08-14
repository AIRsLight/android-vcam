# Product SELinux integration

The HIDL service is intended to run in the platform's existing
`hal_camera_default` domain. Add this directory to the product policy and keep
the executable label in `file_contexts`:

```make
BOARD_VENDOR_SEPOLICY_DIRS += path/to/android_vcam/aosp/provider/sepolicy
```

Do not add broad permissive rules. A product must verify that its existing
camera HAL domain can:

- register `hal_camera_hwservice` and use `vndbinder`;
- load `/vendor/lib64/hw/camera.vcam.so`;
- read provider configuration and frames under `/data/vendor/camera/vcam`;
- use the product graphics mapper/gralloc service.

Type names for `/data/vendor/camera` differ on some vendor policy forks. Add a
narrow allow rule for that product's existing camera data type only when an
actual AVC denial demonstrates it is required.
