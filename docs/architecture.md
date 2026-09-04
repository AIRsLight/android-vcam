# Architecture

## Portable target

```text
Provider configuration -> FrameRenderer / RGB transforms / patterns
                                      |
                 +--------------------+--------------------+
                 |                    |                    |
       legacy camera_module     HIDL Provider 2.4    AIDL Provider
       compatibility proxy      Android 8-12         Android 13+
                 |                    |                    |
                 +---------- CameraService / framework ---+
                                      |
                               scoped target app
```

`FrameRenderer` is independent of Binder, gralloc and OEM Camera3 types. Each
frontend owns stream negotiation, buffer import, metadata and lifecycle, while
all of them consume the same persistent providers, routes and source framing.
`device-probe.sh` emits a normalized capability profile used to select a
frontend or compatibility adapter. Unsupported combinations fail closed.

On Android 8-12, the standalone `camera.vcam` Camera3 module is wrapped by
AOSP's `camera.device@3.4-impl`; this keeps FMQ, buffer-cache, fence and gralloc
ownership in the platform implementation. The `vcam/0` Provider uses public
IDs 1000/1001 so it never collides with OEM IDs 0/1. Enumeration is disabled
unless `ro.vendor.vcam.provider.enabled=true`.

`RouteResolver` is shared by the OEM proxy and standalone module. The latter
accepts `configureStreams` only when the framework-provided client package has
a matching, enabled, non-physical provider in `routes.tsv`. Missing scope,
invalid provider IDs and physical routes are rejected by the standalone module;
CameraService must keep those sessions on the OEM provider. AOSP transports use
the provider-owned
`io.github.androidvcam.clientPackage` session tag, while the current OnePlus
adapter retains its OEM-specific tag.

## Current OnePlus compatibility adapter

```text
Target app -> Camera1 / Camera2 / CameraX -> patched CameraService
  -> OEM package-name session tag
  -> HIDL camera provider -> patched OEM camera.qcom.so
  -> libvcam_proxy.so
       |-- no route / stopped provider -> requested physical camera
       |-- physical-0 / physical-1     -> selected OEM physical camera
       `-- user provider               -> VirtualCamera HAL3
                                           -> YUV / JPEG buffers
```

The original Qualcomm HAL is loaded once. Its ELF receives one `DT_NEEDED`
entry for `libvcam_proxy.so`; the proxy constructor wraps the exported `HMI`
module methods. This avoids loading the proprietary CamX stack twice.

CameraService is patched to preserve the caller package in the OEM
`com.oplus/is.sdk.camera.package` session tag. Static metadata advertises that
tag as an available session/request key. At `configure_streams`, the proxy
reads `/data/vendor/camera/vcam/routes.tsv` and chooses a provider. Unmatched
sessions retain the OEM physical path.

Routes use three tab-separated fields:

```text
package.name<TAB>targetCameraId<TAB>providerId
```

The reserved package field `*` is an explicit global fallback. An exact package
route wins over `*`; when the runtime cannot verify an application identity it
may consult only `*`, never guess a package from ambiguous UID-only traffic.
The manager must present global routing as a separate setting rather than as an
installed application.

Physical providers use fixed logical IDs `physical-0` and `physical-1`; these
mean the discovered primary back and front slots, not literal Camera2 IDs.
The runtime resolves each physical provider through `targets.tsv`, so devices
whose public IDs are swapped, nonzero or nonnumeric keep the same manager
configuration. A configured route to an absent slot fails closed. User provider
frames live at `/data/vendor/camera/vcam/providers/<id>/frame.rgb`; the
presence of `enabled` activates that provider. Missing or disabled providers
currently fall back to the target camera's physical source.

Remote providers run one background `vcam-streamer` process. FFmpeg 4.2.2 is
linked statically into that executable so RTSP/HTTP/HLS availability does not
depend on a vendor `libavformat.so`. The runner enters APatch's `magisk` domain
only to inherit Android's default-network routing; `vcam-streamer` immediately
drops to the `camera` UID before opening a socket or parsing media. It scales
to the configured source limit and atomically publishes planar `VCAMYUV1`
frames. RGB frames from older managers remain readable. The YUV path halves
the provider-frame payload and removes the redundant RGB-to-YUV conversion in
the Camera HAL.

The bundled FFmpeg runner remains the portable fallback across Android 10-14.
It enables frame/slice decoder threading and drops excess decoded live-stream
frames before scaling, preventing low configured FPS from accumulating RTSP
latency. Android hardware decoding is intentionally kept behind a future
module-owned codec-service boundary: this standalone native runner has no
JavaVM for FFmpeg's MediaCodec bridge, and vendor byte-buffer layouts are not
portable enough to make an in-process MediaCodec path a safe default. Such a
service must always fall back to this software/YUV path when configuration or
output-layout negotiation fails.
HTTPS files still use the ROM's BoringSSL-enabled curl before decoding.

Stopping a remote provider waits for its runner and decoder children to exit
before removing transient state or starting a replacement. This prevents an old
runner's exit trap from deleting the new instance's `enabled` marker or temporary
frame during rapid restart/update sequences.

The control daemon forks a short-lived handler per authenticated client. A slow
network preview therefore does not block provider, route, or status reads. Provider
metadata updates use a temporary file and restart active streams; a failed restart
restores the previous metadata and stream configuration.

The manager requests RTSP previews from the same backend decoder used at
runtime. `provider-start` removes stale output and waits up to 15 seconds for a
new first frame, so a missing protocol, route or decoder is returned as an
error instead of leaving a false "running" provider.

The unified module uses the active MetaModule supported by APatch or KernelSU.
Guarded `post-mount.sh` verifies the proxy over the pinned, otherwise
unused `/vendor/lib64/hw/local_time.default.so` slot, then binds CameraService
and the OEM HAL copies. Every source/target hash is checked and the module is
disabled on any mismatch.

## Root-free control plane

The native manager never starts `su` and requests neither broad storage nor
camera access. It discovers installed apps locally and uses Android's document
picker for images and videos. Selected bytes are streamed to the module; the
module owns the durable copy under `/data/adb/android_vcam`, so closing or
uninstalling the manager does not remove routes or stop providers.

Routes are keyed only by package name and remain in `routes.tsv` when an app is
uninstalled. The manager resolves labels and icons at display time, marks missing
packages temporarily unavailable, and reacts to package broadcasts so reinstalling
the same package restores the route presentation without rewriting the route.

`vcamd` listens on an abstract Unix socket in its own SELinux domain. The
kernel-provided `SO_PEERCRED` UID must map to
`io.github.androidvcam.manager` in Android's package database. The binary
accepts only a compiled command whitelist and directly executes `vcamctl`
without a shell, so the IPC cannot be used as a general root command channel.
