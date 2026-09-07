import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("pwsh") and shutil.which("git"), "requires pwsh and git")
class DevReleasePolicyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="vcam-release-policy-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.git("init", "-b", "dev")
        self.git("config", "user.name", "Fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        (self.root / "source.txt").write_text("committed source\n")
        self.git("add", "source.txt")
        self.git("commit", "-m", "fixture")

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args], check=True,
                              text=True, encoding="utf-8", capture_output=True).stdout.strip()

    def guard(self, version="0.5.0-dev.43"):
        return subprocess.run(["pwsh", "-NoProfile", "-File",
                               str(ROOT / "tools/assert-dev-release-source.ps1"),
                               "-RepoRoot", str(self.root), "-Version", version],
                              capture_output=True, text=True, encoding="utf-8", timeout=15)

    def test_clean_dev_returns_exact_commit(self):
        result = self.guard()
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(self.git("rev-parse", "HEAD"), result.stdout.strip())

    def test_main_and_detached_head_are_rejected(self):
        self.git("switch", "-c", "main")
        self.assertNotEqual(0, self.guard().returncode)
        self.git("checkout", "--detach")
        self.assertNotEqual(0, self.guard().returncode)

    def test_uncommitted_source_is_rejected(self):
        (self.root / "source.txt").write_text("uncommitted changes\n")
        self.assertNotEqual(0, self.guard().returncode)

    def test_untracked_source_is_rejected(self):
        (self.root / "new.txt").write_text("untracked source\n")
        self.assertNotEqual(0, self.guard().returncode)

    def test_withdrawn_and_nondev_versions_are_rejected(self):
        for version in ("0.5.0-dev.42", "0.5.0", "../release"):
            with self.subTest(version=version):
                self.assertNotEqual(0, self.guard(version).returncode)

    def test_publication_validates_revision_asset_layout_and_hashes_without_upload(self):
        tools = self.root / "tools"
        tools.mkdir()
        for name in ("assert-dev-release-source.ps1", "publish-dev-release.ps1"):
            shutil.copyfile(ROOT / "tools" / name, tools / name)
        (self.root / ".gitignore").write_text("dist/\n")
        notes = self.root / "notes.md"
        notes.write_text("Fixture notes\n")
        self.git("add", ".")
        self.git("commit", "-m", "fixture publishing scripts")
        dist = self.root / "dist"
        dist.mkdir()
        names = ["android-vcam-module-v0.5.0-dev.43.zip",
                 "android-vcam-manager-v0.5.0-dev.43-debug.apk",
                 "android-vcam-camera2-test-v0.5.0-dev.43-debug.apk"]
        artifacts = []
        for name in names:
            data = ("fixture " + name).encode()
            (dist / name).write_bytes(data)
            artifacts.append(dict(file=name, sha256=hashlib.sha256(data).hexdigest(), bytes=len(data)))
        manifest = dict(release="0.5.0-dev.43", source_branch="dev",
                        source_commit=self.git("rev-parse", "HEAD"), artifacts=artifacts)
        manifest_path = dist / "android-vcam-supported-v0.5.0-dev.43.json"

        def validate():
            manifest_path.write_text(json.dumps(manifest))
            return subprocess.run(["pwsh", "-NoProfile", "-File", str(tools / "publish-dev-release.ps1"),
                                   "-Version", "0.5.0-dev.43", "-NotesFile", str(notes), "-ValidateOnly"],
                                  capture_output=True, text=True, encoding="utf-8", timeout=15)

        result = validate()
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("nothing published", result.stdout)
        manifest["source_branch"] = "main"
        self.assertNotEqual(0, validate().returncode)
        manifest["source_branch"] = "dev"
        artifacts[0]["file"] = "testkit.zip"
        self.assertNotEqual(0, validate().returncode)
        artifacts[0]["file"] = names[0]
        (dist / names[0]).write_bytes(b"changed after manifest")
        self.assertNotEqual(0, validate().returncode)


if __name__ == "__main__":
    unittest.main()
