# Development and device safety

## Non-destructive policy

Development never remounts a partition read-write and never overwrites the OEM
HAL. Tests use temporary bind mounts; the release uses APatch's systemless
module overlay. Rebooting without the module restores the partition files.

The installer checks device, product, API, ABI, fingerprint, OEM HAL hash and
CameraService hash. `post-mount.sh` validates every mounted payload and creates
the module `disable` marker if anything differs.

## Build and test order

1. Run `tools/inspect-device.ps1` and retain the report.
2. Build native binaries and the ordinary test APK.
3. Patch the retained OEM HAL copy with `tools/patch-original-hal.py`.
4. Run host tests and inspect the module ZIP.
5. Confirm APatch module mounting and Safe Mode are available.
6. Install the ZIP. A reboot is required to activate the final overlay.
7. Validate physical fallback first, then scoped physical and virtual routes.

The repository and packaging steps are reversible. Installing the test APK is
also reversible and does not grant it root.

## AOSP compile validation

The Android 12 source integration has a separate WSL validation path. Prepare
an `android-12.0.0_r34` checkout and its Linux build dependencies, then run
`tools/verify-aosp-build.ps1` in the default `Check` mode before using
`-Mode Build`. The build uses `vendor/android_vcam_buildcheck` as a marked
managed source copy and `out/android-vcam-r34` for outputs. It restores the
pristine `frameworks/av` checkout, the original `frameworks/base/Android.bp`,
and temporary Blueprint discovery markers after the compile attempt. Android
12's ncurses 5 compatibility libraries are linked only into the isolated host
output runtime; the WSL system library path is not modified.

The three targets have been compiled successfully for `aosp_arm64-eng` against
the exact `android-12.0.0_r34` frameworks/av commit. Build mode additionally
checks that the CameraService library, virtual HAL, and HIDL provider service
exist in the product output before reporting success.

The validation tool deliberately does not download AOSP projects. Source sync
is an operator-controlled prerequisite because a complete checkout and build
can consume substantial disk space. It also performs no device-side action;
successful compilation is not authorization to flash a system or vendor image.

## Recovery

If Android boots, disable `android_vcam` in APatch and reboot. If it does not,
trigger APatch Safe Mode with the configured volume-down procedure during early
boot so modules are disabled. Recovery/ADB removal of the module directory is
the final fallback.

On the current development device, USB ADB needs a cable replug after every
reboot; plan recovery access before activation.
