# Read-path performance dev.38 release

Version `0.5.0-dev.38` removes repair work and repeated metadata subprocesses
from the provider and route read paths. Status counts providers and routes
directly instead of formatting both complete lists. The Manager caches recent
status, provider, and route results for 30 seconds while retaining explicit
refresh and action-driven invalidation.

| Artifact | SHA-256 |
| --- | --- |
| `android-vcam-manager-v0.5.0-dev.38-debug.apk` | `a09d4433c663422dd2a100890b21006146b1db6028dddd245a639131072433a1` |
| `android-vcam-camera2-test-v0.5.0-dev.38-debug.apk` | `5c8913a476f51c7377e92099d999595bb87e4e132d5afa87eea9272cb738d800` |
| `android-vcam-module-v0.5.0-dev.38.zip` | `aa7b187b2bb6f4f3ea47ebe2b6eda2a86019fdac541c94e5dd4dd352043668ab` |

On the NX769J, the standalone optimized controller preserved byte-for-
byte provider and route output. Average provider query time decreased from
about 0.97 seconds to 0.13 seconds, status from 1.42 seconds to 0.38 seconds,
and routes from 0.31 seconds to 0.16 seconds, including ADB command overhead.
