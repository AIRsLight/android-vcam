# One-shot Android 14 AIDL provider probe

This fingerprint-locked KernelSU module qualifies stock CameraService discovery
of a declared stable-AIDL v2 `vcam/0` provider on the NX769J Android 14 build.
It is not a production release and advertises zero cameras during boot.

The module mounts one VINTF fragment through OverlayFS MetaModule. The fragment
was checked together with a complete VINTF/APEX snapshot from the target using
the Android 14 `checkvintf` host binary: the stock snapshot and the AIDL v2
candidate both report `COMPATIBLE`. The equivalent HIDL 2.4 candidate is
rejected because the device manifest targets FCM 8.

OverlayFS replaces directory inodes as well as the fragment. Every directory
from `vendor/etc` through `vintf/manifest` is therefore explicitly labeled
`vendor_configs_file`; labeling only the XML is insufficient because
servicemanager must be able to traverse the overlay directories.

CameraService calls the blocking AIDL service lookup while initializing every
declared provider. `post-fs-data.sh` therefore launches a background bootstrap
before CameraService starts. The foreground script exits immediately so it
cannot delay MetaModule ordering. The bootstrap retries registration while
servicemanager reloads the mounted VINTF fragment.

The bootstrap first verifies that MetaModule has exposed the fragment, then
writes the module `disable` marker before provider registration. This avoids
racing MetaModule's mount decision while still ensuring that any later reboot
or power loss returns to stock. A 180-second watchdog requests one complete
reboot if Android never reports boot completion. No script restarts
cameraserver.

The action button is read-only for this qualification. After a successful boot,
inspect `/data/adb/android_vcam_aidl_provider/` and verify that the physical
camera list and ordinary camera applications remain healthy. Advertising the
test cameras requires an explicit one-boot arm command while the module is
enabled:

```sh
su -c 'sh /data/adb/modules/android_vcam_aidl_provider/provider-control.sh \
  /data/adb/modules/android_vcam_aidl_provider arm-two'
```

The bootstrap consumes and removes `next-boot.mode` before registration. With
no arm file it always returns to the zero-camera default, and the module still
disables the following boot.
