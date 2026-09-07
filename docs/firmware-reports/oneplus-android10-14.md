# OnePlus Android 10–14 camera compatibility survey

This is an **S1 static** survey of five full-OTA packages. It compares declared
interfaces, init services, SELinux evidence, ELF architecture and build
identities. It does not claim that an untested device can boot the module or
deliver frames.

The qualified comparison point is the OnePlus 7 Pro OxygenOS 12 H.41 device.
The Android 11 package uses a OnePlus 8 because the available same-device
OnePlus 7 Pro archive was structurally incomplete.

## Results

| Android | Sample | Physical Camera Provider | CameraService interface | `cameraserver` ABI | Material difference |
| --- | --- | --- | --- | --- | --- |
| 10 / SDK 29 | OnePlus 7 Pro OOS 10.0.1 | HIDL 2.4 `legacy/0` | HIDL 2.0 | ARM 32-bit | legacy `vendor.oneplus.camera.CameraHIDL` extension |
| 11 / SDK 30 | OnePlus 8 OOS 11.0.0 | HIDL 2.4 `legacy/0` | HIDL 2.1 | ARM 32-bit | additional OnePlus MDM/camera and QTI post-process extensions |
| 12 / SDK 31 | OnePlus 7 Pro OOS 12 H.41 | HIDL 2.4 `legacy/0` | HIDL 2.2 | ARM64 | OnePlus private interfaces move to the OPlus namespace |
| 13 / SDK 33 | OnePlus 9 OOS 13.1 F.30 | HIDL 2.4 `legacy/0` | HIDL 2.2 | ARM64 | OPlus `my_*` partitions and an AIDL camera-extension service appear |
| 14 / SDK 34 | OnePlus 8T OOS 14 H.24 | HIDL 2.4 `legacy/0` | AIDL 1 and HIDL 2.2 coexist | ARM64 | OPlus additionally declares HIDL 2.4 `virtual/0` and a virtual-camera manager |

All five releases launch the physical provider through
`/vendor/bin/hw/android.hardware.camera.provider@2.4-service_64`. The standard
physical-provider boundary therefore remains much more stable than the private
OnePlus/OPlus extensions.

All five also contain an ARM64 `/vendor/lib64/hw/camera.qcom.so` that defines
the standard `HMI` Camera Module symbol, and all five retain the existing
`/vendor/lib64/hw/local_time.default.so` snapshot slot. The firmware analyzer
now emits `portable_global_shim_candidate=true` for every sample.

The generated compatibility signatures are:

| Sample | Signature |
| --- | --- |
| OnePlus 7 Pro / Android 10 | `251e7b9b1c3cd16246d5e111fbf9fec76adc8d6632f55e11ef25738cce307f5a` |
| OnePlus 8 / Android 11 | `92aae34fe21185a5e7823860eabdeaade96afcb7146fbc260aca08d55ba1b1a9` |
| OnePlus 7 Pro / Android 12 | `a733954e09155fdda3d45ae5dd0ccb4dde6727d10b690a6ceac99ccbd23d9e1f` |
| OnePlus 9 / Android 13 | `01127b564d7eb9a8d7e32c354669bae74676300fea251340adb7af353cef7800` |
| OnePlus 8T / Android 14 | `6c165eed8ef6f4ffa2da0b7dce46972abbe43c53776d2187e9af55dc0303a0bd` |

## Compatibility consequences

The useful implementation split is not one recipe per OxygenOS release:

1. **Global routing forms one Android 10–14 ARM64 Provider family.** The current
   API 29 shim loads a device-local snapshot of the OEM Camera Module and wraps
   only its standard `open`/Camera3 boundary. It does not patch CameraService,
   ship a proprietary HAL or depend on OPlus private services.
2. **App-scoped routing remains version-specific.** Android 10–11 run a 32-bit
   `cameraserver`, so package-aware routing on those releases still requires an
   ARM32 CameraService adapter. Android 12–14 can use the ARM64 protocol path.
   This does not block the global adapter.
3. **Android 14 needs provider-instance coexistence detection.** This sample
   declares `virtual/0` through `/odm/bin/hw/virtualcameraprovider` and
   `vendor.oplus.hardware.virtual_device.camera.manager@1.0`. Static inspection
   shows that it is an independent, lazy cross-device camera provider rather
   than a replacement for physical `legacy/0`; it therefore does not conflict
   with the project's reserved `vcam/0`. A generic module must still enumerate
   provider instances and reject activation if that exact `vcam/0` instance is
   already owned. See [OnePlus 8T OEM virtual camera](oneplus8t-oem-virtual-camera.md).
4. **CameraService still varies per build.** Matching the same declared HIDL
   version does not make `libcameraservice.so` binary-compatible. Build IDs and
   runtime invariants must continue to gate patch activation and fail closed.

## Decision

For OnePlus, manufacturer-private code is not currently the primary portability
barrier. A common global adapter is now buildable for Android 10–14. The major
remaining version-specific work is enhanced app-scoped routing: ARM32 for
Android 10/11 and pinned ARM64 protocol profiles for Android 12–14.

The OnePlus qualification sequence is now:

1. **Implemented:** schema 7 reads both CameraService and legacy-module ABIs,
   all Provider instances, OPlus partition layout and the common OnePlus global
   shim candidacy signal;
2. **Implemented:** the API 29 ARM64 shim loads a device-local OEM snapshot,
   preserves original Camera Module metadata in global mode, and packages
   without proprietary firmware;
3. qualify physical pass-through, global color bars/media, auxiliary cameras,
   provider restart and reboot recovery on one device for each Android major;
4. add version-pinned app-scoped protocol adapters after the corresponding
   global path passes hardware qualification.

No proprietary library is linked into the project, and no firmware binary is
disassembled at runtime. The survey uses declarative files and ELF metadata;
actual support still requires staged S2/S3 device tests.

The [offline validation checkpoint](oneplus-offline-validation.md) records the
five-sample dependency audit and executable installer fault tests. All 107 strong
shim imports have candidate exports in each firmware; actual linker namespace
resolution and hardware operation remain unqualified.
