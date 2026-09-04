from __future__ import annotations

import hashlib
import pathlib
import sys
import zipfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_ZIP = ROOT / "dist" / "android-vcam-module-v0.5.0-dev.39.zip"
PROFILES = {
    "oneplus7pro-p202303230244": {
        "install-profile.sh",
        "profile-service.sh",
        "post-mount.sh",
        "system/lib64/libcameraservice.so",
        "vendor/lib64/hw/camera.qcom.so",
        "vendor/lib64/hw/local_time.default.so",
    },
    "nx769j-ukq1-20240417": {
        "install-provider.sh",
        "install-router.sh",
        "provider-service.sh",
        "router-service.sh",
        "post-fs-data.sh",
        "post-mount.sh",
        "system/bin/cameraserver",
        "system/bin/vcamd",
        "system/lib64/libvcam_cameraserver_router.so",
        "system/vendor/etc/vintf/manifest/"
        "android.hardware.camera.provider-service-vcam-v2.xml",
    },
}


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    archive_path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_ZIP
    with zipfile.ZipFile(archive_path) as archive:
        names = set(archive.namelist())
        for root_file in ("module.prop", "customize.sh", "service.sh"):
            if root_file not in names:
                fail(f"unified archive lacks root {root_file}")
        module_prop = archive.read("module.prop").decode("utf-8")
        if "id=android_vcam\n" not in module_prop:
            fail("unified archive has the wrong module ID")
        if any(name.endswith("/module.prop") or name.endswith("/skip_mount") for name in names):
            fail("a selected profile can create an extra module identity or disable MetaModule mounting")
        if any(name.startswith("system/") or name.startswith("vendor/") for name in names):
            fail("unselected system payload escaped the profile container")

        for profile, required_paths in PROFILES.items():
            prefix = f"payload/profiles/{profile}/"
            for relative_path in required_paths:
                if prefix + relative_path not in names:
                    fail(f"{profile} lacks {relative_path}")

        oneplus = "payload/profiles/oneplus7pro-p202303230244/"
        proxy = archive.read(oneplus + "vendor/lib64/libvcam_proxy.so")
        proxy_slot = archive.read(oneplus + "vendor/lib64/hw/local_time.default.so")
        if hashlib.sha256(proxy).digest() != hashlib.sha256(proxy_slot).digest():
            fail("OnePlus MetaModule proxy slot does not match the proxy payload")

        nx = "payload/profiles/nx769j-ukq1-20240417/"
        backend_manifest = archive.read(nx + "payload/backend.sha256").decode("utf-8")
        for line in backend_manifest.splitlines():
            expected_hash, relative_path = line.split(maxsplit=1)
            payload = archive.read(nx + relative_path)
            actual_hash = hashlib.sha256(payload).hexdigest()
            if actual_hash != expected_hash:
                fail(f"NX backend manifest mismatch: {relative_path}")

        nx_service = archive.read(nx + "router-service.sh").decode("utf-8")
        nx_post_fs = archive.read(nx + "post-fs-data.sh").decode("utf-8")
        nx_post_mount = archive.read(nx + "post-mount.sh").decode("utf-8")
        nx_control = archive.read(nx + "provider-control.sh").decode("utf-8")
        nx_policy = archive.read(nx + "sepolicy.rule").decode("utf-8")
        if "ANDROID_VCAM_CURRENT_BOOT_ACTIVE" not in nx_service:
            fail("NX router cannot distinguish current-boot rollback arming")
        if "post-mount.boot-id" not in nx_post_fs or "post-mount.boot-id" not in nx_post_mount:
            fail("NX unified lifecycle lacks the Provider/router boot-order handshake")
        if "/data/adb/android_vcam/runtime/aidl" not in nx_post_fs:
            fail("NX Provider state is not isolated from legacy module uninstall")
        if "/data/adb/android_vcam/runtime/router" not in nx_post_mount:
            fail("NX router state is not owned by the unified module")
        if "unified module remains enabled" not in nx_service:
            fail("NX healthy CameraService path still disables the unified module")
        if "CONFIGURED_MODE_FILE" not in nx_post_fs or "set-route" not in nx_control:
            fail("NX unified Provider mode is not persistent")
        for rule in (
            "allow ksu default_android_service service_manager { add find }",
            "type vcam_camera_data_file",
            "allow cameraserver vcam_camera_data_file file",
        ):
            if rule not in nx_policy:
                fail(f"NX merged SELinux policy lacks: {rule}")
        if "allow cameraserver cameraserver_exec file execute_no_trans" in nx_policy:
            fail("NX merged SELinux policy still permits the obsolete second exec")

    print(f"Unified module archive checks passed: {archive_path}")


if __name__ == "__main__":
    main()
