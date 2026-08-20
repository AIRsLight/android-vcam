# Manual Android 14 provider probe

This development-only KernelSU module validates registration of the AIDL v2
`vcam/0` provider on the pinned NX769J Android 14 firmware.

It deliberately has no `system/` tree, VINTF fragment, `service.sh`, or init
service. The module therefore does not participate in meta-overlayfs and never
starts during boot. Its action button toggles one exact, PID-verified process.
The default empty configuration advertises no camera devices; this separates
provider registration from CameraService device enumeration.

The module action keeps using that zero-camera default. For an explicit ADB
test, set `ANDROID_VCAM_PROBE_ADVERTISE_CAMERAS=1` when invoking `action.sh`;
the provider then loads the upstream AOSP back/front configurations and
advertises internal camera IDs 1000/1001. This mode is never selected at boot.
It also enables a process-local diagnostic color-bar route when no CameraService
client package tag is present. That fallback is gated by the probe environment
and is not active in the production routing path.

After starting the two-camera mode, the bundled direct client can validate
enumeration, characteristics, a 640x480 YUV stream, and one rendered frame per
camera:

```sh
su -c /data/adb/modules/android_vcam_provider_probe/payload/bin/vcam_provider_probe_client
```

The client intentionally requires root because the undeclared diagnostic
service is hidden from the ordinary shell SELinux domain.

To qualify late provider discovery separately from CameraService, start the
client before the provider:

```sh
su -c '/data/adb/modules/android_vcam_provider_probe/payload/bin/vcam_provider_probe_client --watch-registration'
```

It reports whether the exact service is VINTF-declared and waits for a
registration notification for
`android.hardware.camera.provider.ICameraProvider/vcam/0`. This is a transport
diagnostic only; it does not prove that an unpatched stock CameraService will
enumerate the provider.

For this manual registration test, the provider explicitly downgrades its
VINTF-stable Binder object to system stability. This avoids adding an undeclared
vendor-stability Binder to the system service manager. A product-integrated
AOSP/ROM build must keep VINTF stability and declare a dedicated provider
instance. The separate systemless design instead keeps the isolated service at
system stability and requires the version-pinned CameraService discovery patch,
the provider-contract and device-namespace guard, an exact instance allowlist,
and matching SELinux policy; it must not pretend to be a declared vendor HAL.

The vcam service also selects `/dev/binder` explicitly. The upstream Google
service selects `/dev/vndbinder` for its direct C++ Binder dependencies and
relies on the vendor linker namespace to load a separate system Binder copy for
NDK AIDL registration. A binary launched from this module's data path has no
vendor linker namespace, so using the upstream driver selection would send the
registration transaction to the wrong service manager.

Do not add a VINTF fragment to this probe. A previous persistent declaration
made the experimental provider part of early camera discovery and caused a
boot failure on the target device.
