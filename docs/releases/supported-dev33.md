# Persistent unified-module dev.33 release

Version `0.5.0-dev.33` keeps one auto-selected `android_vcam` root module and
changes the qualified NX769J profile from one-shot qualification to persistent
operation. Healthy boots no longer create KernelSU's `disable` marker. Provider,
router, post-mount, registration and boot-watchdog failures still disable the
module before requesting a complete recovery reboot.

| Artifact | SHA-256 |
| --- | --- |
| `android-vcam-manager-v0.5.0-dev.33-debug.apk` | `be131f86939de8ff22770f1a574ae29c858c23e033e25dc3193651c743e4844c` |
| `android-vcam-camera2-test-v0.5.0-dev.33-debug.apk` | `1e3854362d59797a324de4335865346a37876640eb24f6cd3ac098a93ff35f25` |
| `android-vcam-module-v0.5.0-dev.33.zip` | `40a5b51cdf1785eb8ab480795e86dcd7b93e706b1d275d28e52874fbcfff0edb` |

The Provider mode is stored under
`/data/adb/android_vcam/runtime/aidl/configured.mode` and defaults to `route`.
One-boot overrides remain available for engineering diagnostics.

NX769J validation covered an upgrade from disabled dev.32, two consecutive
ordinary boots without reinstallation, persistent `route` mode, seven-camera
enumeration, Provider and CameraService stability, and absence of the module
`disable` marker after both boots. Manager reported `module_enabled=true`,
`mount_active=true` and `physical_route_ready`. The RTSP endpoint was
unreachable during the final second-boot source check; the same route and frame
path had already passed on dev.32, and dev.33 changes only lifecycle state.

The OnePlus 7 Pro payload remains packaged in the same ZIP and still awaits its
physical APatch plus MetaModule regression.
