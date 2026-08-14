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

## Recovery

If Android boots, disable `android_vcam` in APatch and reboot. If it does not,
trigger APatch Safe Mode with the configured volume-down procedure during early
boot so modules are disabled. Recovery/ADB removal of the module directory is
the final fallback.

On the current development device, USB ADB needs a cable replug after every
reboot; plan recovery access before activation.
