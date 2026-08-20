# Product SELinux integration

The HIDL and stable-AIDL services are intended to run in the platform's
existing `hal_camera_default` domain. Add this directory to the product policy
and keep the selected executable label in `file_contexts`:

```make
BOARD_VENDOR_SEPOLICY_DIRS += path/to/android_vcam/aosp/provider/sepolicy
```

The AIDL mapping is deliberately exact: it labels only
`android.hardware.camera.provider-service-vcam`. This is also required for a
systemless delivery because that filename is not covered by the generic
provider expressions present in Android 14 vendor policy.

The accompanying `service_contexts` entry reserves only the stable-AIDL
`ICameraProvider/vcam/0` instance as `hal_camera_service`. A product build must
install this mapping before servicemanager starts. Without it, an additional
systemless instance falls back to `default_android_service`; CameraService
cannot discover that instance unless a wider runtime `find` exception is
added. The one-shot NX769J qualification module carries that exception only
because its MetaModule overlay becomes available after servicemanager has
already loaded service contexts. Production ROM integration must use the exact
mapping instead.

Do not add broad permissive rules. A product must verify that its existing
camera HAL domain can:

- register `hal_camera_hwservice` and use `vndbinder`;
- register and find `hal_camera_service` on the system Binder transport for
  the stable-AIDL frontend;
- load `/vendor/lib64/hw/camera.vcam.so`;
- read provider configuration and frames under `/data/vendor/camera/vcam`;
- use the product graphics mapper/gralloc service.

Type names for `/data/vendor/camera` differ on some vendor policy forks. Add a
narrow allow rule for that product's existing camera data type only when an
actual AVC denial demonstrates it is required.
