# OnePlus 7 Pro Android 12 profile

This profile is restricted to the previously qualified OnePlus 7 Pro firmware.
Other OxygenOS/HydrogenOS builds are separate camera ABIs and fail closed.

| Property | Qualified value |
| --- | --- |
| Profile ID | `oneplus7pro-p202303230244` |
| Device / product | `OnePlus7Pro` / `OnePlus7Pro_CH` |
| Android | 12 / API 31 |
| Fingerprint | `OnePlus/OnePlus7Pro_CH/OnePlus7Pro:12/SKQ1.211113.001/P.202303230244:user/release-keys` |
| OEM Camera HAL SHA-256 | `dab50dfd0bde9f710c92097442d6451695f7ef82cb9e836b0af2b9369751daa6` |
| Accepted legacy HAL SHA-256 | `66d5f38e8a6f5a287a661a06e1224fef477bb41574ca61f7091b5682b9b587d5` |
| Proxy slot SHA-256 | `6ac900f7c1b17fb5551a673ded1fc11469c53dac329bcbbb17b97dd57d2cc992` |
| CameraService SHA-256 | `2108be5d63b385282d844f689e9f34740026072b8ef6daca2ed59b23612870af` |
| Delivery | APatch guarded bind mounts; no MetaModule required |

The qualified adapter provides per-application routing for public camera 0/1.
Physical cameras 0/1, images, color bars, local video and supported network
streams remain available through the shared provider backend. Packages without
a route continue to use the physical camera.

Installation validates device, product, API, ABI, full build fingerprint and
all three mounted camera-stack targets. At runtime schema-5 probing accepts
either the pinned stock hashes or payload hashes from the currently installed
`android_vcam` module. Any mixed or unexpected ABI reports `abi_mismatch`.

The universal Manager performs a root-free fingerprint match and then requires
the backend to return the same profile ID with `profile_status=qualified` before
showing the Camera ABI as verified. The device-specific release artifact is
`android-vcam-oneplus7pro-apm-v0.5.0-dev.30.zip`.
