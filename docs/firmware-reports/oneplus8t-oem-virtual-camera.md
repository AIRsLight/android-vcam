# OnePlus 8T OxygenOS 14 OEM virtual camera

This report records an offline static inspection of the virtual-camera
components in the OnePlus 8T OxygenOS 14 H.24 full OTA. It answers one narrow
compatibility question: whether the OEM `virtual/0` provider conflicts with the
project's `vcam/0` provider or takes over the physical `legacy/0` provider.

## Conclusion

No implementation-level conflict was found. The OPlus component is a separate,
lazy HIDL Camera Provider registered as `virtual/0`. It creates and removes its
own virtual devices on request from a privileged cross-device system service.
It does not register `legacy/0`, load the Qualcomm physical camera module, or
import a Camera Provider client used to proxy the physical provider.

The project's probe therefore records `oem_virtual_provider_present` as
telemetry and rejects activation only when the exact reserved instance
`vcam/0` is already declared or registered.

## Component chain

The firmware contains the following chain:

```text
privileged VDCService / VirtualCameraAdapter
        |
        | protected OPlus manager API
        v
vendor.oplus.hardware.virtual_device.camera.manager/default
        |
        | acquire/release ID, attach/detach, input/output sessions
        v
android.hardware.camera.provider@2.4::ICameraProvider/virtual/0
        |
        | dynamic device@3.3/virtual/<id> status
        v
CameraService -> applications that enumerate the created virtual camera
```

The native service is declared `disabled` and `oneshot` and registers through
the HIDL lazy-service registrar. Its process runs as `cameraserver`, but its
service name remains distinct from the physical provider. The VINTF manifest
lists both `legacy/0` and `virtual/0`; the `override` attribute consolidates the
declaration and does not make the virtual service register `legacy/0`.

## Recovered control contract

The manager interface exposes operations for acquiring and releasing a camera
ID, querying characteristics, opening paired input/output sessions, switching
the active virtual device, and receiving state callbacks. Session operations
configure streams, submit requests, flush, and close. The implementation uses
`Surface`, `GraphicBuffer`, EGL/GLES, libyuv and JPEG processing to move frames
between the cross-device producer and the virtual camera consumer.

Its static configuration advertises up to 30 fps and common output sizes from
640x480 through 1920x1080 in private, YUV and JPEG-related formats. These limits
belong only to the OEM provider and do not constrain this project's renderer.

The system-side APK is a privileged OPlus virtual-device service. Its exported
camera consumers require OPlus/HeyTap protected permissions, and SELinux does
not grant an ordinary untrusted application direct access to the manager
hardware service. Consequently, merely opening a camera from a third-party app
does not activate this path.

## Coexistence requirements

Coexistence still has observable framework behavior, but none of it is an
instance collision:

- CameraService may receive dynamic add/remove status callbacks for OEM virtual
  camera IDs while the OPlus cross-device feature is active.
- Routing and topology code must identify only this project's own provider and
  camera IDs. It must not classify every provider containing the word
  `virtual` as internal.
- Simultaneous OEM and project virtual sessions can add graphics, memory and
  encoder load. The OEM provider is lazy and does not reserve physical camera
  resources merely because it is declared.
- The OEM manager is not used as a project integration point. No proprietary
  firmware library is linked or shipped.

## Evidence boundary

This is static evidence from VINTF/init/SELinux policy, ELF dependencies and
exported interface metadata. It is sufficient to classify provider-instance
ownership and remove the false collision gate. It is not a claim that every
OEM cross-device workflow has been exercised dynamically; simultaneous-session
stress remains part of device qualification rather than activation gating.
