# Provider frame format

Each user provider owns an optional frame:

```text
/data/vendor/camera/vcam/providers/<provider-id>/frame.rgb
```

The file is labeled `vendor_camera_data_file`, owned by `camera:camera`, and is
atomically replaced by `vcam-publisher` or `vcam-streamer`.

## Common header

All integers are unsigned little-endian values.

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 8 | ASCII format magic: `VCAMRGB1` or `VCAMYUV1` |
| 8 | 4 | width |
| 12 | 4 | height |
| 16 | 4 | payload size for the selected format |
| 20 | 4 | change sequence |
| 24 | variable | format-specific pixel payload |

## VCAMRGB1

The compatibility format stores packed RGB888 rows. Its payload is
`width * height * 3`. Existing providers and the legacy WebUI remain valid.

## VCAMYUV1

The high-resolution format stores planar I420: the full-size Y plane followed
by quarter-size Cb and Cr planes. Width and height must be even; payload size is
`width * height * 3 / 2`. Network, local-video and newly imported image
providers use this format so the Camera HAL can fill YUV buffers without an
RGB-to-YUV conversion.

Dimensions must be 1..4096. The manager preserves the selected image's aspect
ratio and bounds it by the source resolution selected at creation time. The
maximum configured frame is 4096 on either edge and 4096x3072 total pixels.
The controller also caps `width * height * fps` at the former 1080p60 pixel
budget, allowing 4K at 15 fps and 12.6 MP at 9 fps. The background decoder
likewise uses the selected maximum dimensions and 1..60 fps. The Camera HAL
applies that budget again to the largest configured client output stream. This
keeps a 3840x2160 client surface at no more than 15 fps even when its provider
contains a smaller 30 or 60 fps source.

Each provider also owns runtime configuration beside `frame.rgb`:

```text
source.cfg       fps,max-width,max-height
view-0.cfg       rotation,scale-milli,center-x-milli,center-y-milli
view-1.cfg       rotation,scale-milli,center-x-milli,center-y-milli
```

Rotation is 0/90/180/270 degrees, scale is 100..8000 (0.10x..8.00x), and normalized centers
are 0..1000. The manager reads the two physical camera sensor dimensions and
shows their aspect-ratio viewports over a source preview. The HAL applies the
saved target-specific crop, pan, zoom and sensor-orientation compensation while
producing YUV or JPEG output. An enabled
pattern provider without a frame produces moving color bars. A disabled or
missing provider causes routing to fall back to the target physical camera.
