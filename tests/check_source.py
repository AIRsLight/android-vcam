from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    required = [
        ROOT / "hal" / "Android.bp",
        ROOT / "hal" / "src" / "VirtualCamera.cpp",
        ROOT / "hal" / "src" / "VirtualCameraModule.cpp",
        ROOT / "hal" / "src" / "VirtualCameraStandaloneModule.cpp",
        ROOT / "native" / "proxy_bootstrap.cpp",
        ROOT / "native" / "stream_provider.c",
        ROOT / "native" / "control_daemon.c",
        ROOT / "apmodule" / "module.prop",
        ROOT / "apmodule" / "customize.sh",
        ROOT / "apmodule" / "device-probe.sh",
        ROOT / "apmodule" / "vcamctl",
        ROOT / "apmodule" / "webroot" / "app.js",
        ROOT / "aosp" / "provider" / "hidl" / "Android.bp",
        ROOT / "aosp" / "provider" / "hidl" / "VcamProvider.cpp",
        ROOT / "aosp" / "provider" / "hidl" / "service.cpp",
        ROOT / "aosp" / "provider" / "aidl" / "Android.bp",
        ROOT / "aosp" / "provider" / "aidl" / "VcamCameraProviderHwl.cpp",
        ROOT / "aosp" / "provider" / "aidl" / "android-13" / "hardware-google-camera.patch",
        ROOT / "aosp" / "provider" / "aidl" / "android-14" / "hardware-google-camera.patch",
        ROOT / "aosp" / "provider" / "aidl" / "android-14" /
        "android.hardware.camera.provider-service-vcam.xml",
        ROOT / "aosp" / "provider" / "sepolicy" / "file_contexts",
        ROOT / "hal" / "include" / "vcam" / "FrameRenderer.h",
        ROOT / "hal" / "include" / "vcam" / "RouteResolver.h",
        ROOT / "hal" / "include" / "vcam" / "ScopedCameraRouter.h",
        ROOT / "hal" / "src" / "FrameRenderer.cpp",
        ROOT / "hal" / "src" / "RouteResolver.cpp",
        ROOT / "hal" / "src" / "ScopedCameraRouter.cpp",
        ROOT / "tests" / "route_resolver_test.cpp",
        ROOT / "tests" / "scoped_camera_router_test.cpp",
        ROOT / "aosp" / "cameraservice" / "android-12" / "frameworks-av.patch",
        ROOT / "aosp" / "cameraservice" / "android-14" / "frameworks-av.patch",
        ROOT / "aosp" / "cameraservice" / "android-12" / "frameworks-base-license.bp",
        ROOT / "tools" / "create-module-zip.py",
        ROOT / "tools" / "apply-aosp-cameraservice-patch.ps1",
        ROOT / "tools" / "verify-aosp-build.ps1",
        ROOT / "tools" / "verify-aosp14-build.sh",
        ROOT / "tools" / "probe-device.ps1",
        ROOT / "docs" / "device-support.md",
        ROOT / "tools" / "build-ffmpeg-android.sh",
    ]
    for path in required:
        if not path.is_file():
            fail(f"missing required file: {path.relative_to(ROOT)}")

    host_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    if "-UNDEBUG" not in host_cmake or "route_resolver_test" not in host_cmake or \
            "scoped_camera_router_test" not in host_cmake:
        fail("native assertions or route resolver test are not enabled")

    module = (ROOT / "apmodule" / "module.prop").read_text(encoding="utf-8")
    if not re.search(r"^id=android_vcam$", module, re.MULTILINE):
        fail("unexpected APModule id")

    installer = (ROOT / "apmodule" / "customize.sh").read_text(encoding="utf-8")
    expected_hash = "dab50dfd0bde9f710c92097442d6451695f7ef82cb9e836b0af2b9369751daa6"
    if expected_hash not in installer:
        fail("installer is not pinned to the inspected original HAL")
    if not (ROOT / "apmodule" / "skip_mount").is_file():
        fail("direct bind-mount delivery must not require an APatch metamodule")
    for required_symbol in ("matches_installed_payload", "INSTALLED_MODULE_DIR"):
        if required_symbol not in installer:
            fail(f"installer is missing safe in-place upgrade support: {required_symbol}")

    for shell_script in (ROOT / "apmodule").glob("*.sh"):
        raw = shell_script.read_bytes()
        if b"\r\n" in raw:
            fail(f"APatch script uses CRLF: {shell_script.name}")
        if not raw.startswith(b"#!/system/bin/sh\n"):
            fail(f"invalid Android shell shebang: {shell_script.name}")

    source = (ROOT / "hal" / "src" / "VirtualCamera.cpp").read_text(encoding="utf-8")
    for required_symbol in (
        "CAMERA_DEVICE_API_VERSION_3_5",
        "CAMERA3_JPEG_BLOB_ID",
        "ANDROID_SENSOR_TIMESTAMP",
        "setSourcePath",
        "kMaxStreamDimension",
        "kMaxOutputPixelRate",
        "outputFrameDurationNs_",
    ):
        if required_symbol not in source:
            fail(f"HAL is missing expected symbol: {required_symbol}")
    if "width == 640 && height == 480" in source:
        fail("HAL must not restrict clients to a fixed preview-size shortlist")
    virtual_camera_header = (ROOT / "hal" / "include" / "vcam" / "VirtualCamera.h").read_text(
        encoding="utf-8"
    )
    if "FrameRenderer frameRenderer_" not in virtual_camera_header:
        fail("HAL does not use the transport-neutral FrameRenderer")
    if "partialResultCount_" not in virtual_camera_header or \
            "result.partial_result = partialResultCount_" not in source:
        fail("HAL frontend partial-result contract is not configurable")

    hal_blueprint = (ROOT / "hal" / "Android.bp").read_text(encoding="utf-8")
    if "VirtualCameraStandaloneModule.cpp" not in hal_blueprint or \
            "VirtualCameraModule.cpp" in hal_blueprint:
        fail("AOSP camera.vcam must use the standalone Camera3 module entrypoint")
    root_blueprint = (ROOT / "Android.bp").read_text(encoding="utf-8")
    provider_blueprint = (
        ROOT / "aosp" / "provider" / "hidl" / "Android.bp"
    ).read_text(encoding="utf-8")
    if 'name: "libvcam_headers"' not in root_blueprint or \
            '"libvcam_headers"' not in provider_blueprint:
        fail("AOSP provider does not import the shared vcam header contract")

    proxy = (ROOT / "native" / "proxy_bootstrap.cpp").read_text(encoding="utf-8")
    for required_symbol in (
        "RouteResolver", "packageFrom",
        "providerForPackage", "gProxyModuleMethods",
    ):
        if required_symbol not in proxy:
            fail(f"OEM proxy is missing expected feature: {required_symbol}")
    if "state->cameraId, 2" not in proxy:
        fail("OEM proxy must preserve the physical camera's two-part result contract")
    route_resolver = (ROOT / "hal" / "src" / "RouteResolver.cpp").read_text(
        encoding="utf-8"
    )
    for required_symbol in ("routesPath", "physical-", "enabled", "frame.rgb"):
        if required_symbol not in route_resolver:
            fail(f"route resolver is missing expected feature: {required_symbol}")
    scoped_router = "\n".join(path.read_text(encoding="utf-8") for path in (
        ROOT / "hal" / "include" / "vcam" / "ScopedCameraRouter.h",
        ROOT / "hal" / "src" / "ScopedCameraRouter.cpp",
    ))
    for required_symbol in ("1000", "1001", "configured", "available"):
        if required_symbol not in scoped_router:
            fail(f"scoped camera router is missing expected feature: {required_symbol}")
    cameraservice_patch = (
        ROOT / "aosp" / "cameraservice" / "android-12" / "frameworks-av.patch"
    ).read_text(encoding="utf-8")
    for required_symbol in (
        "libvcam_route_core", "ScopedCameraRouter", "selectedCameraId",
        "kVcamClientPackageTag", "mClientPackageName",
        "resolveScopedCameraId", "firstPackageNameForUid",
        "routedConfigurations", "getLegacyParametersLazy(cameraId, selectedCameraId",
        "readOnlyParams",
    ):
        if required_symbol not in cameraservice_patch:
            fail(f"Android 12 CameraService patch is missing: {required_symbol}")

    controller = (ROOT / "apmodule" / "vcamctl").read_text(encoding="utf-8")
    for command in (
        "capabilities", "provider-add", "provider-remove", "provider-start", "route-set",
        "provider-publish-stdin", "provider-import-media", "source-preview",
        "provider-frame", "provider-update",
        "route-save",
    ):
        if command not in controller:
            fail(f"provider controller is missing command: {command}")
    if '--thumbnail "$frame" "$preview" 640 640' not in controller:
        fail("provider preview must use a bounded backend thumbnail")

    publisher = (ROOT / "native" / "frame_publisher.c").read_text(encoding="utf-8")
    for required_symbol in (
        "--thumbnail", "O_NOFOLLOW", "MAX_DIMENSION", "MAX_PIXELS",
        "kYuvMagic", "FRAME_I420", "pread_exact",
    ):
        if required_symbol not in publisher:
            fail(f"frame publisher lacks safe thumbnail support: {required_symbol}")

    streamer = (ROOT / "native" / "stream_provider.c").read_text(encoding="utf-8")
    for required_symbol in (
        "AV_PIX_FMT_YUV420P", "MAX_SOURCE_DIMENSION", "MAX_SOURCE_PIXELS",
        "MAX_PIXEL_RATE", "kYuvMagic",
    ):
        if required_symbol not in streamer:
            fail(f"stream provider lacks high-resolution YUV support: {required_symbol}")

    probe = (ROOT / "apmodule" / "device-probe.sh").read_text(encoding="utf-8")
    for required_symbol in (
        "provider_transport", "provider_version", "provider_instance", "adapter_hint",
        "aidl_service_line", "provider_service_hash", "legacy_module_hash",
        "cameraservice_hash", "root_manager",
    ):
        if required_symbol not in probe:
            fail(f"device probe is missing field: {required_symbol}")

    provider = "\n".join(
        (ROOT / "aosp" / "provider" / "hidl" / name).read_text(encoding="utf-8")
        for name in ("VcamProvider.h", "VcamProvider.cpp")
    )
    for required_symbol in (
        "setCallback", "getCameraIdList", "CameraDevice", "camera.vcam",
        "device@3.4/vcam/1000", "device@3.4/vcam/1001",
        "ro.vendor.vcam.provider.enabled",
    ):
        if required_symbol not in provider:
            fail(f"AOSP HIDL provider is missing symbol: {required_symbol}")
    provider_service = (ROOT / "aosp" / "provider" / "hidl" / "service.cpp").read_text(
        encoding="utf-8"
    )
    if 'registerAsService("vcam/0")' not in provider_service:
        fail("AOSP HIDL provider does not register the vcam/0 instance")
    aidl_provider = "\n".join(path.read_text(encoding="utf-8") for path in (
        ROOT / "aosp" / "provider" / "aidl" / "Android.bp",
        ROOT / "aosp" / "provider" / "aidl" / "VcamCameraProviderHwl.cpp",
        ROOT / "aosp" / "provider" / "aidl" / "android-13" / "hardware-google-camera.patch",
    ))
    for required_symbol in (
        "camera_service_vcam_defaults", "ANDROID_CAMERA_HWL_LIBRARY",
        "libvcam_googlecamerahwl_impl", "kBackCameraId = 1000",
        "kFrontCameraId = 1001", "kVcamClientPackageTag",
        "libvcam_frame_core", "libvcam_route_core", "ConfigureRoutedFrame",
        "VcamSetActiveFrame", "VcamRenderYuv420", "VcamRenderRgb",
        "android.hardware.camera.provider-service-vcam-v2",
    ):
        if required_symbol not in aidl_provider:
            fail(f"AOSP AIDL provider is missing symbol: {required_symbol}")
    aidl_v2_manifest = (
        ROOT / "aosp" / "provider" / "aidl" / "android-14" /
        "android.hardware.camera.provider-service-vcam.xml"
    ).read_text(encoding="utf-8")
    if "<version>2</version>" not in aidl_v2_manifest:
        fail("Android 14 provider manifest does not declare stable AIDL v2")
    provider_file_contexts = (
        ROOT / "aosp" / "provider" / "sepolicy" / "file_contexts"
    ).read_text(encoding="utf-8")
    if "android\\.hardware\\.camera\\.provider-service-vcam" not in provider_file_contexts:
        fail("Android 14 provider executable lacks a hal_camera_default_exec mapping")
    cameraservice14_patch = (
        ROOT / "aosp" / "cameraservice" / "android-14" / "frameworks-av.patch"
    ).read_text(encoding="utf-8")
    for required_symbol in (
        "resolveScopedCameraId", "routedClientPackageName",
        "kVcamClientPackageTag", "mClientPackageName",
    ):
        if required_symbol not in cameraservice14_patch:
            fail(f"Android 14 CameraService patch is missing: {required_symbol}")
    vendor_tags = (ROOT / "hal" / "include" / "vcam" / "VendorTags.h").read_text(
        encoding="utf-8"
    )
    for required_symbol in ("io.github.androidvcam", "clientPackage", "com.oplus"):
        if required_symbol not in vendor_tags:
            fail(f"frontend vendor-tag contract is missing: {required_symbol}")

    manager_manifest = (ROOT / "manager" / "AndroidManifest.xml").read_text(encoding="utf-8")
    manager_sources = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (ROOT / "manager" / "src").rglob("*.java")
    )
    for forbidden in ("ProcessBuilder", '"su"', "MANAGE_EXTERNAL_STORAGE"):
        if forbidden in manager_manifest or forbidden in manager_sources:
            fail(f"root-free manager contains forbidden capability: {forbidden}")
    for required_symbol in (
        "source-preview", "loadBackendNetworkPreview", "showProviderPreview",
        "provider-frame", "刷新帧", "MAX_SOURCE_PIXEL_RATE", "12 MP",
    ):
        if required_symbol not in manager_sources:
            fail(f"manager lacks source preview support: {required_symbol}")

    service_scripts = controller + \
        (ROOT / "apmodule" / "service.sh").read_text(encoding="utf-8") + \
        (ROOT / "apmodule" / "boot-completed.sh").read_text(encoding="utf-8")
    for required_symbol in ("autostart", "retry-provider", "camera-dump.txt"):
        if required_symbol not in service_scripts:
            fail(f"provider boot recovery is missing: {required_symbol}")

    test_app = (ROOT / "testapp" / "src" / "io" / "github" / "androidvcam" /
                "test" / "CameraPreviewActivity.java").read_text(encoding="utf-8")
    for required_symbol in ("preview_width", "preview_height", "single_stream"):
        if required_symbol not in test_app:
            fail(f"Camera2 test app lacks high-resolution launch support: {required_symbol}")
    for required_symbol in ("LocalSocket", "VCAMD001", "SO_PEERCRED", "MANAGER_PACKAGE"):
        daemon_and_manager = manager_sources + (ROOT / "native" / "control_daemon.c").read_text(encoding="utf-8")
        if required_symbol not in daemon_and_manager:
            fail(f"authenticated manager transport is missing: {required_symbol}")

    native_build = (ROOT / "native" / "CMakeLists.txt").read_text(encoding="utf-8")
    if "libavformat.a" not in native_build or "device_avformat" in native_build:
        fail("vcam-streamer must use the pinned static Android FFmpeg SDK")

    print("Source layout checks passed")


if __name__ == "__main__":
    main()
