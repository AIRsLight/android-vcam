# Provider frame format

Each user provider owns an optional frame:

```text
/data/vendor/camera/vcam/providers/<provider-id>/frame.rgb
```

The file is labeled `vendor_camera_data_file`, owned by `camera:camera`, and is
atomically replaced by `vcam-publisher` or `vcam-streamer`.

## VCAMRGB1

All integers are unsigned little-endian values.

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 8 | ASCII `VCAMRGB1` |
| 8 | 4 | width |
| 12 | 4 | height |
| 16 | 4 | payload size (`width * height * 3`) |
| 20 | 4 | change sequence |
| 24 | variable | packed RGB888 rows |

Dimensions must be 1..4096. The manager preserves the selected image's aspect
ratio and bounds it by the source resolution selected at creation time. The
background decoder likewise uses the selected maximum dimensions and 1..60 fps.

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
