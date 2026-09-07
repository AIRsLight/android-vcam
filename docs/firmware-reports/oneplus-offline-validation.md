# OnePlus offline adapter validation — 2026-09-07

This checkpoint covers the five Qualcomm OnePlus firmware samples on the Linux
analysis worker. No phone or AVD was connected. It is S1 evidence, not hardware
qualification and not certification of every OnePlus device running Android 10–14.
Other chipsets and Camera HAL layouts need a separate compatibility path.

## Native dependency audit

The API 29 ARM64 shim SHA-256 is
`bb56372d06e1f88c9ad8c69807372511cff46b29750cb54d1715310fa03288cf`.
It exports a visible 344-byte `HMI` OBJECT and imports 107 strong symbols through
six direct dependencies: `libc.so`, `libm.so`, `libdl.so`, `liblog.so`,
`libhardware.so`, and `libcamera_metadata.so`.

| Firmware sample | OEM HMI size | Direct dependency candidates | Strong imports found |
| --- | --- | --- | --- |
| OnePlus 7 Pro OOS 10.0.1 / Android 10 | 344 | 6/6 | 107/107 |
| OnePlus 8 OOS 11.0.0 / Android 11 | 344 | 6/6 | 107/107 |
| OnePlus 7 Pro OOS 12 H.41 / Android 12 | 344 | 6/6 | 107/107 |
| OnePlus 9 OOS 13.1 F.30 / Android 13 | 344 | 6/6 | 107/107 |
| OnePlus 8T OOS 14 H.24 / Android 14 | 344 | 6/6 | 107/107 |

The audit reads ELF tables without executing the OEM files. Symbol versions are
matched and AArch64 IFUNC exports are recognized, including older readelf's
`<OS specific>: 10` representation. The result uses the union of exports from
candidate library paths. It does **not** prove that the vendor linker namespace
will select those files: bootstrap/system/VNDK/APEX visibility and actual loader
relocations remain unverified. Full generated evidence records each candidate
path, SHA-256 and matched imports in
`out/firmware-inspection/oneplus-shim-dependency-audit.json`.

Reproduce on the firmware analysis worker:

```sh
python3 tools/firmware/audit_oneplus_shim.py \
  --cohort-root /aosp/firmware/oneplus \
  --shim out/native/arm64-v8a/camera.qcom.so \
  --output out/firmware/oneplus-shim-dependency-audit.json
```

## Installer and mount-check execution

`tests/test_oneplus_module_lifecycle.py` runs the production scripts under Linux
bash in temporary filesystem trees. Only absolute Android paths and the root
manager/property APIs are substituted. File copying, hashing, rejection branches
and marker creation execute normally. The suite covers all SDKs 29–34 under both
KSU and APatch, upgrade snapshot preservation, invalid input and partial overlays.

Fault injection reproduced and fixed six defects in the initial prototype:

- an old mounted shim could be saved as the OEM original when its snapshot was missing;
- a corrupted old snapshot was not compared to the installed profile's hash;
- an old firmware's snapshot could be retained after an OTA;
- architecture fields alone accepted a file without the ELF magic;
- post-mount checks did not invalidate a changed firmware fingerprint;
- disabling the unified module without rebooting could capture its still-mounted patched HAL.

The mount-check failure flag disables the module for a subsequent boot and
withholds `mount.ok`. It does **not** unmount the current boot's overlays or
guarantee recovery from a camera-provider crash occurring before the check runs.
A pre-mount/one-shot recovery design still needs actual MetaModule lifecycle
qualification before this adapter can become an unattended release profile.

The archive check now validates the actual dynamic-symbol export of `HMI`,
including its type, visibility and size. A matching string in a binary is no
longer accepted as proof of the Camera Module interface.

```sh
python3 -m unittest tests.test_oneplus_module_lifecycle \
  tests.test_oneplus_dependency_audit tests.test_firmware_analyzer
```

These tests do not emulate Android linker behavior, MetaModule mounts, SELinux,
Camera HAL initialization, gralloc/fences, mechanical camera activation, image
delivery or device reboot. Those remain the next runtime qualification gates.

## Verified engineering artifact

The combined Linux suites above plus `tests.test_capability_evaluator` passed
29 tests without skips. Source layout, shell syntax and the ELF-aware archive
check also passed. Native and manager sources were unchanged in this checkpoint.

- Module: `dist/android-vcam-oneplus-global-v0.5.0-dev.41.zip`
- SHA-256: `53d334a3de6ee2616a63d6992af63785ed02aaed678314031b9633dc53cd81c0`
- This is an engineering artifact; the qualified single-module release remains unchanged.
