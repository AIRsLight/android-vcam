#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "firmware" / "analyze_firmware.py"
SPEC = importlib.util.spec_from_file_location("analyze_firmware", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class FirmwareAnalyzerTest(unittest.TestCase):
    def create_fixture(self, root: pathlib.Path, transport: str = "hidl") -> None:
        system = root / "system"
        vendor = root / "vendor"
        (system / "etc" / "vintf").mkdir(parents=True)
        (vendor / "etc" / "vintf").mkdir(parents=True)
        (vendor / "etc" / "init").mkdir(parents=True)
        (vendor / "etc" / "selinux").mkdir(parents=True)
        (system / "build.prop").write_text(
            "ro.build.version.release=12\n"
            "ro.build.version.sdk=31\n"
            "ro.build.fingerprint=OnePlus/test/test:12/example:user/release-keys\n"
            "ro.product.name=test\nro.product.device=test\n",
            encoding="utf-8",
        )
        if transport == "hidl":
            manifest = """<manifest version="1.0" type="device" target-level="6">
              <hal format="hidl"><name>android.hardware.camera.provider</name>
              <transport>hwbinder</transport><version>2.5</version><interface>
              <name>ICameraProvider</name><instance>legacy/0</instance></interface></hal>
              </manifest>"""
        else:
            manifest = """<manifest version="2.0" type="device" target-level="8">
              <hal format="aidl"><name>android.hardware.camera.provider</name>
              <version>2</version><interface><name>ICameraProvider</name>
              <instance>internal/0</instance></interface></hal></manifest>"""
        (vendor / "etc" / "vintf" / "camera.xml").write_text(manifest, encoding="utf-8")
        (vendor / "etc" / "init" / "camera.rc").write_text(
            "service vendor.camera-provider /vendor/bin/hw/camera-provider\n"
            "    class hal\n    user cameraserver\n"
            "on property:vendor.camera.ready=1\n    start vendor.camera-provider\n",
            encoding="utf-8",
        )
        (vendor / "etc" / "selinux" / "vendor_service_contexts").write_text(
            "vendor.camera u:object_r:hal_camera_service:s0\n", encoding="utf-8"
        )

    def test_extracts_camera_contract(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.create_fixture(root)
            report = MODULE.analyze(root, "fixture")
            self.assertEqual(report["build"]["sdk"], "31")
            self.assertEqual(report["signature_inputs"]["provider_transport"], "hidl")
            self.assertEqual(report["signature_inputs"]["target_levels"], ["6"])
            self.assertEqual(
                report["signature_inputs"]["provided_camera_hals"][0]["name"],
                "android.hardware.camera.provider",
            )
            self.assertEqual(report["camera_services"][0]["name"], "vendor.camera-provider")
            self.assertTrue(report["selinux_camera_evidence"])

    def test_comparison_marks_transport_change_high_risk(self) -> None:
        with tempfile.TemporaryDirectory() as left_dir, tempfile.TemporaryDirectory() as right_dir:
            left = pathlib.Path(left_dir)
            right = pathlib.Path(right_dir)
            self.create_fixture(left, "hidl")
            self.create_fixture(right, "aidl")
            comparison = MODULE.compare(MODULE.analyze(right, "new"), MODULE.analyze(left, "old"))
            self.assertFalse(comparison["same_signature"])
            self.assertEqual(comparison["difference_risk"], "high")
            self.assertIn(
                "signature_inputs.provider_transport",
                {item["field"] for item in comparison["changes"]},
            )

    def test_vendor_aidl_extension_does_not_relabel_standard_provider(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.create_fixture(root)
            manifest_path = root / "vendor" / "etc" / "vintf" / "camera.xml"
            manifest = manifest_path.read_text(encoding="utf-8").replace(
                "</manifest>",
                "<hal format=\"aidl\"><name>vendor.oplus.hardware.cameraextension</name>"
                "<version>1</version><fqname>ICameraExtensionService/default</fqname>"
                "</hal></manifest>",
            )
            manifest_path.write_text(manifest, encoding="utf-8")
            report = MODULE.analyze(root, "fixture")
            self.assertEqual(report["signature_inputs"]["provider_transport"], "hidl")

    def test_top_level_comments_do_not_leak_into_service(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            rc = root / "vendor" / "etc" / "init" / "services.rc"
            rc.parent.mkdir(parents=True)
            rc.write_text(
                "service unrelated /vendor/bin/unrelated\n"
                "    class hal\n"
                "\n"
                "# camera provider is declared in another file\n",
                encoding="utf-8",
            )
            self.assertEqual(MODULE.parse_init_services(rc, root), [])

    def test_camera_group_does_not_relabel_unrelated_service(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            rc = root / "vendor" / "etc" / "init" / "services.rc"
            rc.parent.mkdir(parents=True)
            rc.write_text(
                "service unrelated /vendor/bin/unrelated\n"
                "    class hal\n"
                "    group audio camera\n",
                encoding="utf-8",
            )
            self.assertEqual(MODULE.parse_init_services(rc, root), [])

    def test_ignores_broken_partition_symlinks(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            self.create_fixture(root)
            camera_dir = root / "system" / "lib64"
            camera_dir.mkdir(parents=True)
            try:
                (camera_dir / "libcamera-broken.so").symlink_to(
                    "/system/product/priv-app/missing/libcamera.so"
                )
            except OSError as error:
                self.skipTest(f"host cannot create symlinks: {error}")
            report = MODULE.analyze(root, "fixture")
            self.assertEqual(report["build"]["sdk"], "31")


if __name__ == "__main__":
    unittest.main()
