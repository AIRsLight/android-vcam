# Offline firmware compatibility analysis

Public full-OTA packages can extend discovery beyond the small real-device lab,
but they produce an **S1 static** result only. A firmware report never qualifies
boot stability, provider registration, frame delivery, caller attribution or a
third-party application.

## Admission boundary

A sample is admitted only for an exact retail SKU that has reproducible evidence
of bootloader unlocking and persistent `su`. Carrier-locked variants, temporary
root exploits, paid remote unlock services and unverified region conversions are
excluded. Eligibility is recorded per SKU rather than inferred from the product
family.

Raw OEM archives and extracted proprietary binaries stay on the analysis worker.
The repository stores only source URLs and checksums supplied by the publisher,
our generated JSON/Markdown reports, and compatibility signatures.

## Extraction on the Linux analysis worker

Install `payload-dumper-go`, `debugfs`, `simg2img`, and `fsck.erofs`, then run:

```sh
tools/firmware/extract_ota.sh firmware.zip /aosp/firmware/oneplus/example
python3 tools/firmware/analyze_firmware.py \
  /aosp/firmware/oneplus/example/partitions \
  --label oneplus-example \
  --output out/firmware/oneplus-example.json \
  --markdown out/firmware/oneplus-example.md
```

Add `--compare BASELINE.json` to classify changes in the Android/FCM level,
Camera Provider transport and instances, camera init services, and core
CameraService binary identities.

## Initial OnePlus cohort

The first cohort deliberately isolates the transition where practical:

| Android | Sample | Purpose |
| --- | --- | --- |
| 10 | OnePlus 7 Pro global retail | pre-OPlus OnePlus integration baseline |
| 11 | OnePlus 8 global retail | pre-OPlus fallback; the available 7 Pro mirror is structurally incomplete |
| 12 | OnePlus 7 Pro global retail | compare with the already qualified device |
| 13 | OnePlus 9 North America retail | post-OPlus integration representative |
| 14 | OnePlus 8T India retail | unlockable OPlus-derived Android 14 representative |

The machine-readable source list is
`tools/firmware/cohorts/oneplus-android10-14.json`. A missing checksum or an
indirect mirror URL blocks unattended admission: the worker records the resolved
URL, byte size and SHA-256 before extraction. Firmware source availability and
bootloader/SU eligibility are deliberately tracked as separate facts.

The OnePlus Updater device catalogue marks the corresponding unlocked retail
families as enabled and the T-Mobile variants as disabled, but this is only
download eligibility evidence. Bootloader/SU eligibility remains an independent
admission field.

## Signature boundary

The deterministic signature includes:

- Android release/SDK and VINTF target level;
- declared AIDL/HIDL Camera HALs, versions and instances;
- camera-related init service names and commands;
- ELF architecture/bitness and GNU Build IDs (or hashes when no Build ID exists)
  of CameraService core files.

For OPlus/ColorOS-derived builds, the extractor also includes `my_product`,
`my_manifest`, `my_region`, and `my_carrier`. Large application/content
partitions such as `my_stock`, `my_heytap`, and `my_bigball` are excluded from
the compatibility signature unless a later probe proves that they own a camera
service contract.

It intentionally excludes runtime camera IDs, proprietary vendor-tag semantics,
buffer/fence timing and camera resource conflicts. Those require a read-only
runtime probe and then staged real-device qualification.
