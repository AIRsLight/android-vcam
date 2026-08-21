# One-shot Android 14 AIDL provider probe

This fingerprint-locked KernelSU module qualifies stock CameraService discovery
of a declared stable-AIDL v2 `vcam/0` provider on the NX769J Android 14 build.
It is not a production release. It advertises zero cameras by default and can
advertise the two test cameras for one explicitly armed boot.

dev.38 packages the shared manager-independent backend: `vcamd`,
`vcamctl`, `provider-runner.sh`, `vcam-publisher` and the statically linked
arm64 `vcam-streamer`. Provider metadata remains under
`/data/adb/android_vcam/providers`, while frames remain under
`/data/vendor/camera/vcam/providers`; uninstalling the manager APK therefore
does not stop configured providers or remove their state. The installer checks
the complete backend payload against `payload/backend.sha256` before enabling
the module.

Network-backed autostart providers retry for at most 18 rounds after
`boot_completed`, with ten seconds between unsuccessful rounds, and stop as soon
as every pending RTSP/HTTP/HLS source has published its first frame. This covers
late Wi-Fi association without creating an unbounded boot worker.

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

The stock NX769J service contexts reserve only the OEM camera-provider
instances. Because MetaModule becomes active after servicemanager has loaded
those contexts, `vcam/0` is registered as `default_android_service`. This probe
adds only the registration and CameraService lookup permissions needed for that
systemless fallback. A built-in ROM integration must instead install the exact
`vcam/0 -> hal_camera_service` mapping from `aosp/provider/sepolicy`.

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

To exercise the production file-provider path on the stock CameraService,
create routes for `io.github.androidvcam.test`, enable the selected provider
under `/data/vendor/camera/vcam/providers/`, and arm the diagnostic route:

```sh
su -c 'sh /data/adb/modules/android_vcam_aidl_provider/provider-control.sh \
  /data/adb/modules/android_vcam_aidl_provider arm-route'
```

`arm-route` advertises both test devices and supplies that diagnostic package
name only when the stock CameraService omits the VCAM session vendor tag. The
provider still resolves `routes.tsv`, verifies the enabled marker, reads
`frame.rgb`, and applies the normal per-camera transform and source pacing. A
patched production CameraService supplies the real client package in session
parameters and never enables this fallback.

After the AIDL Provider registers, the post-fs-data worker explicitly starts
the authenticated abstract-socket backend and resumes only providers carrying
an explicit `autostart` marker. This placement is required because KernelSU
skips the module's normal late-start service after the one-shot probe writes
its next-boot `disable` marker. A boot-ID guard prevents duplicate startup on
root managers that still invoke `service.sh`. Providers that fail before Wi-Fi
routing is ready receive one further attempt five seconds after Android reports
boot completion. This does not
restart cameraserver or the OEM camera provider. KernelSU children keep the
`ksu` domain; the legacy APatch build continues to use its dedicated `vcamd`
transition and returns privileged media work to the `magisk` domain. Removing
this probe suspends its running media providers while preserving their
configuration for a later compatible backend installation.
