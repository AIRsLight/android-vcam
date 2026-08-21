# NX769J Android 14 dev.29 integration snapshot

This snapshot freezes the qualified NX769J UKQ1 implementation before the
project begins general device-recipe automation. It is valid only for the exact
fingerprint and camera-library hashes recorded in the
[device profile](../device-profiles/nx769j-android14.md).

## Deliverables

| Artifact | SHA-256 |
| --- | --- |
| `android-vcam-aidl-provider-v0.5.0-dev.29.zip` | `A3FC4A03F3CA693C9CEDAEFB8FF1BF48C9FE776B5F2AC965595FB3BC1165285F` |
| `android-vcam-portable-bootstrap-v0.5.0-dev.29-physical-route.zip` | `E1350E860936C68398350D0F8C521BC19E159167730B1DCD3D135D7DBA4E02A6` |
| `android-vcam-manager-v0.5.0-dev.29-debug.apk` | `EA1ECE252C1B5C974851C4C5A9CFD11FEA75BEF2F23EB2C640B78443C037D390` |
| `android-vcam-camera2-test-v0.5.0-dev.29-debug.apk` | `F4F822D2166A13C0AAF48567BEEC1B5509282426AC0596679DA1AD4A1417FE91` |

The two module ZIPs use canonical entry timestamps and reproduce the same hash
when rebuilt from the same tracked inputs. APK hashes identify the delivered
debug-signed files; the current APK build pipeline is not yet reproducible.

## Manager integration

The root-free Manager contains an exact local fingerprint matcher and consumes
the backend schema-4 capability profile when the module is active. A qualified
match requires both the NX769J build fingerprint and the pinned
`libcameraservice.so`/`libcamera_client.so` hashes. The overview page reports
router state, virtual-provider state and one-shot rollback protection, and can
launch the ordinary test APK against public camera 0 or 1.

The Manager never enables, installs or grants privileges to the root modules.
When the backend is absent it reports the certified local profile but leaves
runtime routing status explicitly unknown; it does not mistake a backend crash
for proof of stock state. KernelSU remains responsible for installation,
verification and explicit one-shot activation.

## Qualified boundary

- scoped public 0/1 routing to hidden internal 1000/1001;
- local and RTSP frame delivery with independent target transforms;
- listener lifecycle/filtering and CaptureRequest repair;
- unscoped OEM camera remains physical;
- delayed Wi-Fi association recovers network providers automatically;
- both modules arm next-boot stock rollback;
- Manager and test APK operate without root.

Delegated system-camera intents and broad third-party application coverage are
not part of this snapshot. They remain explicit follow-up work rather than being
silently treated as qualified behavior.
