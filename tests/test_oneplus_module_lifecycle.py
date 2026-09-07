"""Execute production installer/mount checks in a temporary Linux filesystem.

Only absolute Android paths are relocated. Android property and installer API
calls are mocked; copying, hashing, branching and marker creation are real.
This does not emulate MetaModule mounts, SELinux, HAL loading or device boot.
"""
from __future__ import annotations

import hashlib
import os
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "requires Linux bash")
class OnePlusModuleLifecycleTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="vcam-lifecycle-")
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        self.module = self.root / "staging"
        self.installed = self.root / "data/adb/modules/android_vcam_oneplus_global"
        self.hal = self.root / "vendor/lib64/hw/camera.qcom.so"
        self.slot = self.hal.with_name("local_time.default.so")
        self.snapshot = self.module / "system/vendor/lib64/hw/local_time.default.so"
        self.shim = self.snapshot.with_name("camera.qcom.so")
        self.marker = self.root / "data/adb/android_vcam/mount.ok"
        self.stock = self.elf(b"stock")
        self.write(self.hal, self.stock)
        self.write(self.slot, b"original local time library")
        self.write(self.shim, self.elf(b"new shim"))
        self.write(self.root / "data/adb/metamodule/module.prop", b"metamodule=true\n")
        for name in ("customize.sh", "post-mount.sh"):
            source = (ROOT / "oneplus-global-module" / name).read_text()
            source = re.sub(r"(?<![\w/])/(data/adb|vendor/lib64/hw)(?=[/\s\"])",
                            lambda m: self.root.as_posix() + m.group(0), source)
            self.write(self.module / name, source.encode())
        self.environment = dict(os.environ, MODPATH=str(self.module), KSU="true", APATCH="false",
                                TEST_SDK="29", TEST_BRAND="OnePlus", TEST_ABI="arm64-v8a",
                                TEST_FINGERPRINT="OnePlus/test:10/stock")

    @staticmethod
    def elf(label: bytes) -> bytes:
        data = bytearray(70000)
        data[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<H", data, 18, 183)
        data[100:100 + len(label)] = label
        return bytes(data)

    @staticmethod
    def write(path: pathlib.Path, data: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def run_script(self, name: str) -> subprocess.CompletedProcess:
        wrapper = r'''
abort() { printf '%s\n' "$*" >&2; exit 42; }
ui_print() { printf '%s\n' "$*"; }
set_perm() { :; }
getprop() {
    case "$1" in
        ro.product.manufacturer|ro.product.brand) printf '%s\n' "$TEST_BRAND" ;;
        ro.build.version.sdk) printf '%s\n' "$TEST_SDK" ;;
        ro.product.cpu.abi) printf '%s\n' "$TEST_ABI" ;;
        ro.build.fingerprint) printf '%s\n' "$TEST_FINGERPRINT" ;;
    esac
}
. "$1"
'''
        script = str(self.module / name)
        return subprocess.run(["bash", "-c", wrapper, script, script],
                              env=self.environment, capture_output=True, text=True, timeout=15)

    def install(self) -> None:
        result = self.run_script("customize.sh")
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def stage_old_install(self, *, snapshot: bytes | None = None) -> None:
        old_shim = self.elf(b"old shim")
        self.write(self.installed / "system/vendor/lib64/hw/camera.qcom.so", old_shim)
        self.write(self.hal, old_shim)
        if snapshot is not None:
            self.write(self.installed / "system/vendor/lib64/hw/local_time.default.so", snapshot)
        profile = ("fingerprint=" + self.environment["TEST_FINGERPRINT"] + "\n"
                   "original_camera_hal_sha256=" + hashlib.sha256(self.stock).hexdigest() + "\n")
        self.write(self.installed / "oneplus-profile.conf", profile.encode())

    def test_fresh_install_all_sdk_and_root_managers(self) -> None:
        for sdk in range(29, 35):
            for manager in ("KSU", "APATCH"):
                with self.subTest(sdk=sdk, manager=manager):
                    self.environment.update(TEST_SDK=str(sdk), KSU="false", APATCH="false")
                    self.environment[manager] = "true"
                    self.install()
                    self.assertEqual(self.stock, self.snapshot.read_bytes())
                    self.assertEqual(self.stock, self.hal.read_bytes())

    def test_rejects_unsupported_device_and_missing_metamodule(self) -> None:
        for key, value in (("TEST_SDK", "28"), ("TEST_SDK", "35"),
                           ("TEST_BRAND", "Other"), ("TEST_ABI", "x86_64")):
            with self.subTest(key=key, value=value):
                previous = self.environment[key]
                self.environment[key] = value
                self.assertEqual(42, self.run_script("customize.sh").returncode)
                self.environment[key] = previous
        (self.root / "data/adb/metamodule/module.prop").unlink()
        self.assertEqual(42, self.run_script("customize.sh").returncode)

    def test_upgrade_preserves_original_snapshot(self) -> None:
        self.stage_old_install(snapshot=self.stock)
        self.install()
        self.assertEqual(self.stock, self.snapshot.read_bytes())

    def test_disabled_unified_module_must_be_unmounted_before_switch(self) -> None:
        unified = self.root / "data/adb/modules/android_vcam"
        patched = self.elf(b"unified patched HAL")
        self.write(unified / "disable", b"")
        self.write(unified / "vendor/lib64/hw/camera.qcom.so", patched)
        self.write(self.hal, patched)
        self.assertEqual(42, self.run_script("customize.sh").returncode)
        # After reboot the stock file is visible again and switching is safe.
        self.write(self.hal, self.stock)
        self.install()
        self.assertEqual(self.stock, self.snapshot.read_bytes())

    def test_upgrade_rejects_missing_snapshot(self) -> None:
        self.stage_old_install()
        self.assertEqual(42, self.run_script("customize.sh").returncode)

    def test_upgrade_rejects_corrupt_snapshot(self) -> None:
        self.stage_old_install(snapshot=self.elf(b"corrupt stock"))
        self.assertEqual(42, self.run_script("customize.sh").returncode)

    def test_upgrade_rejects_snapshot_from_previous_firmware(self) -> None:
        self.stage_old_install(snapshot=self.stock)
        self.environment["TEST_FINGERPRINT"] += "/ota"
        self.assertEqual(42, self.run_script("customize.sh").returncode)

    def test_rejects_non_elf_with_forged_architecture_fields(self) -> None:
        self.write(self.hal, b"BAD!" + self.stock[4:])
        self.assertEqual(42, self.run_script("customize.sh").returncode)

    def test_mount_gate_checks_both_overlay_paths(self) -> None:
        self.install()
        for shim_mounted, snapshot_mounted in ((False, False), (True, False), (False, True)):
            with self.subTest(shim=shim_mounted, snapshot=snapshot_mounted):
                (self.module / "disable").unlink(missing_ok=True)
                self.write(self.marker, b"stale marker")
                self.write(self.hal, self.shim.read_bytes() if shim_mounted else self.stock)
                self.write(self.slot, self.stock if snapshot_mounted else b"original local time")
                result = self.run_script("post-mount.sh")
                self.assertNotEqual(0, result.returncode)
                self.assertFalse(self.marker.exists())
                self.assertTrue((self.module / "disable").exists())
        (self.module / "disable").unlink()
        self.write(self.hal, self.shim.read_bytes())
        self.write(self.slot, self.stock)
        self.assertEqual(0, self.run_script("post-mount.sh").returncode)
        self.assertTrue(self.marker.exists())

    def test_mount_gate_rejects_firmware_change(self) -> None:
        self.install()
        self.write(self.hal, self.shim.read_bytes())
        self.write(self.slot, self.stock)
        self.environment["TEST_FINGERPRINT"] += "/ota"
        self.assertNotEqual(0, self.run_script("post-mount.sh").returncode)
        self.assertFalse(self.marker.exists())


if __name__ == "__main__":
    unittest.main()
