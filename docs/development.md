# Development and device safety

## Non-destructive policy

Development never remounts a partition read-write and never overwrites the OEM
HAL. Tests use temporary bind mounts; the release uses APatch's systemless
module overlay. Rebooting without the module restores the partition files.

The installer checks device, product, API, ABI, fingerprint, OEM HAL hash and
CameraService hash. `post-mount.sh` validates every mounted payload and creates
the module `disable` marker if anything differs.

## Build and test order

1. Run `tools/inspect-device.ps1` and retain the report.
2. Build native binaries and the ordinary test APK.
3. Patch the retained OEM HAL copy with `tools/patch-original-hal.py`.
4. Run host tests and inspect the module ZIP.
5. Confirm APatch module mounting and Safe Mode are available.
6. Install the ZIP. A reboot is required to activate the final overlay.
7. Validate physical fallback first, then scoped physical and virtual routes.

The repository and packaging steps are reversible. Installing the test APK is
also reversible and does not grant it root.

## AOSP compile validation

The Android 12 source integration has a separate WSL validation path. Prepare
an `android-12.0.0_r34` checkout and its Linux build dependencies, then run
`tools/verify-aosp-build.ps1` in the default `Check` mode before using
`-Mode Build`. The build uses `vendor/android_vcam_buildcheck` as a marked
managed source copy and `out/android-vcam-r34` for outputs. It restores the
pristine `frameworks/av` checkout, the original `frameworks/base/Android.bp`,
and temporary Blueprint discovery markers after the compile attempt. Android
12's ncurses 5 compatibility libraries are linked only into the isolated host
output runtime; the WSL system library path is not modified.

The three targets have been compiled successfully for `aosp_arm64-eng` against
the exact `android-12.0.0_r34` frameworks/av commit. Build mode additionally
checks that the CameraService library, virtual HAL, and HIDL provider service
exist in the product output before reporting success.

The validation tool deliberately does not download AOSP projects. Source sync
is an operator-controlled prerequisite because a complete checkout and build
can consume substantial disk space. It also performs no device-side action;
successful compilation is not authorization to flash a system or vendor image.

Android 13 uses the native Linux validator `tools/verify-aosp13-build.sh` and
the exact `android-13.0.0_r84` source tag. On the remote builder, initialize a
space-conscious platform checkout with the official manifest groups
`pdk,pdk-fs,-cts,-darwin,-device,-developers`; these retain CameraService,
Soong, VINTF, AIDL/HIDL interfaces and the generic arm64 product while
excluding CTS, device-specific kernels, sample trees and the macOS toolchain.
Run `tools/sync-aosp13-build-deps.sh` after that base sync to fetch the
supplemental native dependency closure discovered by the camera-only build.
The helper accepts `--proxy-url`, keeps proxy variables local to its process,
and requires an explicit `--force-sync` before repo may replace a conflicting
project checkout.

The validator temporarily applies the versioned patch from
`aosp/cameraservice/android-13` and restores `frameworks/av` on exit. Build
mode uses `m --soong-only`, output directory
`out/android-vcam-r84-soong`, and verifies the 64-bit CameraService library,
virtual camera HAL, and HIDL provider service. On modern Linux hosts it wraps
only the legacy RenderScript compiler with AOSP's bundled ncurses 5 runtime;
the host's system libraries and global library search path remain unchanged.

An interrupted first shallow sync can leave `.repo/projects/<path>.git`
without its `shallow` file. Repo treats that state as a deliberately
unshallowed checkout and silently omits `--depth=1` on the next fetch, which
can download the complete history of large prebuilt repositories. Before
retrying an incomplete checkout, verify the child `git fetch` command contains
`--depth=1`; if it does not, stop that sync and repair or recreate only the
incomplete project checkout before retrying. Never remove a completed AOSP
project merely to repair another project's shallow state.

The current workstation proxy already accepts LAN clients. The builder reaches
it directly at `http://192.168.130.151:1085`; no SSH reverse tunnel is needed.
For example:

```bash
tools/sync-aosp13-build-deps.sh \
    --aosp-root /aosp/src/android-13.0.0_r84 \
    --proxy-url http://192.168.130.151:1085
tools/verify-aosp13-build.sh \
    --aosp-root /aosp/src/android-13.0.0_r84 \
    --mode build --jobs 8
```

The proxy address is a development-network setting, not a runtime dependency
of the module, manager, or device-side backend.

For the Android 13+ frontend, `tools/verify-aosp13-aidl-baseline.sh` separately
builds the tag-pinned Google Camera HAL AIDL service and EmulatedCamera HWL. It
creates only a deterministic build-number file under the isolated output
directory. Passing this baseline proves the upstream AIDL transport can be
built; it does not mean the android-vcam HWL adapter is complete.

Android 14 uses `tools/verify-aosp14-build.sh` against the exact
`android-14.0.0_r23` tag. Check mode verifies the CameraService routing and
public-boundary patch stack plus the Google Camera HWL patch, leaving both
checkouts pristine when it returns. Build mode temporarily applies the
patches, compiles the stable-AIDL v2 provider,
and verifies the linked routing, metadata and frame-rendering hook symbols
before restoring the source trees. The resulting frontend advertises outputs
up to 4096x3072 and applies source-aware, pixel-budgeted frame pacing. This is
a tier-1 AOSP/ROM integration check; it is not a systemless stock-ROM package
and must not be installed on an arbitrary OEM Android 14 device.

After initializing the space-conscious Android 14 base checkout, use the
version-specific dependency helper before the validator. It synchronizes only
the explicit camera-target closure encountered at r23, including both JDK 17
for Soong and JDK 11 for Bazel; it does not request a complete AOSP checkout:

```bash
tools/sync-aosp14-build-deps.sh \
    --aosp-root /aosp/src/android-14.0.0_r23
tools/verify-aosp14-build.sh \
    --aosp-root /aosp/src/android-14.0.0_r23 \
    --mode build --jobs 8
```

If the builder needs a proxy, append `--proxy-url http://host:port` to the
dependency synchronization command. The proxy is exported only for that run.

## Recovery

If Android boots, disable `android_vcam` in APatch and reboot. If it does not,
trigger APatch Safe Mode with the configured volume-down procedure during early
boot so modules are disabled. Recovery/ADB removal of the module directory is
the final fallback.

On the current development device, USB ADB needs a cable replug after every
reboot; plan recovery access before activation.
