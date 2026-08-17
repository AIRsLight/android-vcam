# Architecture

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

Physical providers are fixed IDs `physical-0` and `physical-1`. User provider
frames live at `/data/vendor/camera/vcam/providers/<id>/frame.rgb`; the
presence of `enabled` activates that provider. Missing or disabled providers
fail closed to the target camera's physical source.

Remote providers run one background `vcam-streamer` process. FFmpeg 4.2.2 is
linked statically into that executable so RTSP/HTTP/HLS availability does not
depend on a vendor `libavformat.so`. The runner enters APatch's `magisk` domain
only to inherit Android's default-network routing; `vcam-streamer` immediately
drops to the `camera` UID before opening a socket or parsing media. It scales
to the configured source limit and atomically publishes `VCAMRGB1` frames.
HTTPS files still use the ROM's BoringSSL-enabled curl before decoding.

The control daemon forks a short-lived handler per authenticated client. A slow
network preview therefore does not block provider, route, or status reads. Provider
metadata updates use a temporary file and restart active streams; a failed restart
restores the previous metadata and stream configuration.

The manager requests RTSP previews from the same backend decoder used at
runtime. `provider-start` removes stale output and waits up to 15 seconds for a
new first frame, so a missing protocol, route or decoder is returned as an
error instead of leaving a false "running" provider.

The module declares `skip_mount`, so it does not depend on an APatch
metamodule. Guarded `post-mount.sh` binds the proxy over the pinned, otherwise
unused `/vendor/lib64/hw/local_time.default.so` slot, then binds CameraService
and the OEM HAL copies. Every source/target hash is checked and the module is
disabled on any mismatch.

## Root-free control plane

The native manager never starts `su` and requests neither broad storage nor
camera access. It discovers installed apps locally and uses Android's document
picker for images and videos. Selected bytes are streamed to the module; the
module owns the durable copy under `/data/adb/android_vcam`, so closing or
uninstalling the manager does not remove routes or stop providers.

`vcamd` listens on an abstract Unix socket in its own SELinux domain. The
kernel-provided `SO_PEERCRED` UID must map to
`io.github.androidvcam.manager` in Android's package database. The binary
accepts only a compiled command whitelist and directly executes `vcamctl`
without a shell, so the IPC cannot be used as a general root command channel.
