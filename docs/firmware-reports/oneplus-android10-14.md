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

1. **Android 12–14 form one ARM64 family.** The current generic engine can keep
   the same integration boundary, while selecting a verified patch recipe by
   CameraService build identity and runtime probes. OPlus private services are
   capabilities to detect, not compile-time dependencies.
2. **Android 10–11 require a separate ARM32 execution path.** The Camera Provider
   is still 64-bit, but the process being patched (`cameraserver`) is 32-bit.
   Supporting these releases requires an ARM32 patcher/trampoline, ARM32 media
   dependencies and ABI-aware module payload selection; a profile change alone
   is insufficient.
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
barrier. The major new work is ARM32 support for Android 10/11; Android 12–14
mostly needs build-specific recipes behind one ARM64 runtime-probe framework.

The OnePlus qualification sequence is now:

1. **Implemented:** schema 7 reads the process ABI, all Provider instances,
   CameraService AIDL/HIDL registrations and OPlus `my_*` partition layout;
2. record OEM `virtual/0` as coexistence telemetry and reject only an existing
   `vcam/0`;
3. qualify another ARM64 OnePlus Android 13/14 device before treating the OPlus
   path as a reusable family;
4. implement ARM32 only if Android 10/11 remains a release requirement, then
   qualify it separately from ARM64.

No proprietary library is linked into the project, and no firmware binary is
disassembled at runtime. The survey uses declarative files and ELF metadata;
actual support still requires staged S2/S3 device tests.
