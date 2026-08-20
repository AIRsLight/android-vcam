# Android 14 systemless HIDL provider probe

This development-only KernelSU module is retained for Android releases whose
device manifest targets FCM 7 or older. It adds one HIDL 2.4 camera provider
declaration through the installed OverlayFS MetaModule. The provider registers
during KernelSU's post-fs-data stage, before the HAL class and CameraService
start.

It must not be installed on the pinned NX769J Android 14 firmware. Offline
validation with the Android 14 `checkvintf` binary shows that its target FCM 8
accepts only the stable-AIDL camera-provider transport and rejects HIDL 2.4 as
deprecated. `customize.sh` independently reads the active vendor manifest and
aborts when `target-level` is 8 or newer.

The qualification is deliberately a zero-camera provider. It can prove that
the declared provider is accepted without changing the public camera list.
After successful early registration it immediately writes the module's
`disable` marker. The current boot keeps its already-mounted fragment and
running process, while every subsequent boot returns to stock unless a
developer explicitly arms another one-shot test.

The current package remains fingerprint-locked while the next legacy-FCM test
device is qualified. A declaration is persistent for as long as the module is
enabled, so installation requires a reboot and retains the same KernelSU
bootloop-protection recovery procedure used for other mounted modules.
It also starts a 180-second watchdog: if Android never reports boot completion,
the watchdog confirms the disabled marker and requests one complete recovery
reboot. It never restarts cameraserver in isolation. This does not replace
KernelSU's own bootloop protection.
It must not be generalized to a new ROM until that ROM's VINTF, HIDL transport,
SELinux labels and CameraService behavior have been qualified.
