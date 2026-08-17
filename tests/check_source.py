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
        ROOT / "native" / "proxy_bootstrap.cpp",
        ROOT / "native" / "stream_provider.c",
        ROOT / "native" / "control_daemon.c",
        ROOT / "apmodule" / "module.prop",
        ROOT / "apmodule" / "customize.sh",
        ROOT / "apmodule" / "vcamctl",
        ROOT / "apmodule" / "webroot" / "app.js",
        ROOT / "tools" / "create-module-zip.py",
        ROOT / "tools" / "build-ffmpeg-android.sh",
    ]
    for path in required:
        if not path.is_file():
            fail(f"missing required file: {path.relative_to(ROOT)}")

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
        "PatternGenerator::fillYuv420",
        "CAMERA3_JPEG_BLOB_ID",
        "ANDROID_SENSOR_TIMESTAMP",
        "setSourcePath",
        "kMaxStreamDimension",
    ):
        if required_symbol not in source:
            fail(f"HAL is missing expected symbol: {required_symbol}")
    if "width == 640 && height == 480" in source:
        fail("HAL must not restrict clients to a fixed preview-size shortlist")

    proxy = (ROOT / "native" / "proxy_bootstrap.cpp").read_text(encoding="utf-8")
    for required_symbol in (
        "routes.tsv", "physical-0", "physical-1", "packageFrom",
        "providerForPackage", "gProxyModuleMethods",
    ):
        if required_symbol not in proxy:
            fail(f"OEM proxy is missing expected feature: {required_symbol}")

    controller = (ROOT / "apmodule" / "vcamctl").read_text(encoding="utf-8")
    for command in (
        "provider-add", "provider-remove", "provider-start", "route-set",
        "provider-publish-stdin", "provider-import-media", "source-preview",
        "provider-frame", "provider-update",
        "route-save",
    ):
        if command not in controller:
            fail(f"provider controller is missing command: {command}")
    if '--thumbnail "$frame" "$preview" 640 640' not in controller:
        fail("provider preview must use a bounded backend thumbnail")

    publisher = (ROOT / "native" / "frame_publisher.c").read_text(encoding="utf-8")
    for required_symbol in ("--thumbnail", "O_NOFOLLOW", "MAX_DIMENSION", "pread_exact"):
        if required_symbol not in publisher:
            fail(f"frame publisher lacks safe thumbnail support: {required_symbol}")

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
        "provider-frame", "刷新帧",
    ):
        if required_symbol not in manager_sources:
            fail(f"manager lacks source preview support: {required_symbol}")
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
