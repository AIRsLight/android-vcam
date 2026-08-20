# Manual Android 14 provider probe

This development-only KernelSU module validates registration of the AIDL v2
`vcam/0` provider on the pinned NX769J Android 14 firmware.

It deliberately has no `system/` tree, VINTF fragment, `service.sh`, or init
service. The module therefore does not participate in meta-overlayfs and never
starts during boot. Its action button toggles one exact, PID-verified process.
The default empty configuration advertises no camera devices; this separates
provider registration from CameraService device enumeration.

For this manual registration test only, the provider explicitly downgrades its
VINTF-stable Binder object to system stability. This avoids adding an undeclared
vendor-stability Binder to the system service manager. Production delivery must
keep VINTF stability and declare a dedicated provider instance.

The vcam service also selects `/dev/binder` explicitly. The upstream Google
service selects `/dev/vndbinder` for its direct C++ Binder dependencies and
relies on the vendor linker namespace to load a separate system Binder copy for
NDK AIDL registration. A binary launched from this module's data path has no
vendor linker namespace, so using the upstream driver selection would send the
registration transaction to the wrong service manager.

Do not add a VINTF fragment to this probe. A previous persistent declaration
made the experimental provider part of early camera discovery and caused a
boot failure on the target device.
