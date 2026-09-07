"""Privacy and non-invasive behavior of the cached diagnostics command."""
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "requires Linux bash")
class DiagnosticsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="vcam-diagnostics-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.module = self.root / "module"
        self.module.mkdir()
        self.state = self.root / "data/adb/android_vcam"
        self.state.mkdir(parents=True)
        controller = (ROOT / "apmodule/vcamctl").read_text()
        controller = re.sub(r"(?<![\w/])/(data/adb|data/vendor/camera/vcam)(?=[/\s\"])",
                            lambda m: self.root.as_posix() + m.group(0), controller)
        (self.module / "vcamctl").write_text(controller)
        (self.module / "module.prop").write_text("id=android_vcam\nversion=dev.test\n")

    def collect(self):
        # Any camera probing or app enumeration must fail this test.
        wrapper = r'''
dumpsys() { echo FORBIDDEN_DUMPSYS; return 99; }
logcat() { echo FORBIDDEN_LOGCAT; return 99; }
pm() { echo FORBIDDEN_PM; return 99; }
getprop() { echo FORBIDDEN_GETPROP; return 99; }
export -f dumpsys logcat pm getprop
exec bash "$1" diagnostics
'''
        before = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        result = subprocess.run(["bash", "-c", wrapper, "test", str(self.module / "vcamctl")],
                                capture_output=True, text=True, timeout=3)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertNotIn("FORBIDDEN", result.stdout)
        after = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        self.assertEqual(before, after)
        return result.stdout

    def test_missing_backend_profile_is_reported_without_probing(self):
        report = self.collect()
        self.assertIn("profile_cache_present=false", report)
        self.assertIn("module.version=dev.test", report)
        self.assertIn("mount_marker_present=false", report)

    def test_private_routes_sources_and_unknown_cache_fields_are_excluded(self):
        (self.state / "device-profile.conf").write_text(
            "sdk=29\nprofile_status=probe_required\nprovider_instance=legacy/0\n"
            "serial=SECRET_SERIAL\nsource=rtsp://user:SECRET_PASSWORD@192.168.1.2/live\n"
            "hardware=https://private/token\nbrand=" + "x" * 500 + "\n")
        (self.state / "providers").mkdir()
        (self.state / "providers/meta").write_text("PRIVATE_MEDIA_AND_URL")
        frame_root = self.root / "data/vendor/camera/vcam"
        frame_root.mkdir(parents=True)
        (frame_root / "routes.tsv").write_text("com.private.app\t0\tprivate_source")
        report = self.collect()
        self.assertIn("device.sdk=29", report)
        self.assertIn("device.provider_instance=legacy/0", report)
        for secret in ("SECRET", "PRIVATE", "com.private.app", "192.168", "private/token", "x" * 200):
            self.assertNotIn(secret, report)

    def test_large_corrupt_profile_is_bounded_and_deduplicated(self):
        (self.state / "device-profile.conf").write_text("sdk=29\n" * 10000)
        (self.module / "disable").touch()
        (self.state / "mount.ok").touch()
        report = self.collect()
        self.assertEqual(1, report.count("device.sdk="))
        self.assertLess(len(report), 2048)
        self.assertIn("module_enabled=false", report)
        self.assertIn("mount_marker_present=true", report)


if __name__ == "__main__":
    unittest.main()
