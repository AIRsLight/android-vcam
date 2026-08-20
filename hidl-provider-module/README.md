# Android 14 systemless HIDL provider probe

This development-only KernelSU module tests the stock CameraService discovery
path on the pinned NX769J Android 14 firmware. It adds one HIDL 2.4 camera
provider declaration through the installed OverlayFS MetaModule. The provider
itself starts only after Android reports boot completion and CameraService is
available, so it is not an early-boot executable dependency.

The boot default is deliberately a zero-camera provider. It can prove that the
declared provider is accepted without changing the public camera list. The
KernelSU action button switches to two test devices (`1000` and `1001`) and
back to the zero-camera mode by restarting only the exact provider process.

This probe is fingerprint-locked. A declaration is persistent for as long as
the module is enabled, so installation requires a reboot and retains the same
KernelSU bootloop-protection recovery procedure used for other mounted modules.
It also starts a 180-second watchdog during post-fs-data: if Android never
reports boot completion, the watchdog disables this module and requests one
recovery reboot. This does not replace KernelSU's own bootloop protection.
It must not be generalized to a new ROM until that ROM's VINTF, HIDL transport,
SELinux labels and CameraService behavior have been qualified.
