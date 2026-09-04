from __future__ import annotations

import argparse
import pathlib
import zipfile


REQUIRED = {
    "module.prop",
    "skip_mount",
    "customize.sh",
    "device-probe.sh",
    "run-probe.sh",
    "service.sh",
    "action.sh",
    "uninstall.sh",
    "README.md",
}

FORBIDDEN_ROOTS = ("system/", "vendor/", "odm/", "product/", "system_ext/")
FORBIDDEN_NAMES = {"sepolicy.rule", "post-fs-data.sh", "post-mount.sh"}
FORBIDDEN_PAYLOAD_WORDS = (
    "vcam-publisher",
    "vcam-streamer",
    "vcamd",
    "vcam_cameraserver_launcher",
    "android.hardware.camera.provider-service-vcam",
)
MUTATING_SCRIPT_WORDS = (
    "/data/vendor/camera",
    "setprop ctl.",
    "service call",
    "start-provider",
    "physical-route",
    "mount -",
)


def check_archive(path: pathlib.Path) -> None:
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        missing = REQUIRED - names
        if missing:
            raise SystemExit(f"missing required entries: {sorted(missing)}")
        if any(name.startswith(FORBIDDEN_ROOTS) for name in names):
            raise SystemExit("probe archive contains a partition overlay")
        if FORBIDDEN_NAMES & names:
            raise SystemExit("probe archive contains a lifecycle or SELinux mutation")
        if any(name.endswith((".so", ".apk", ".jar")) for name in names):
            raise SystemExit("probe archive contains a binary payload")

        module_prop = archive.read("module.prop").decode("utf-8")
        if "id=android_vcam_capability_probe\n" not in module_prop:
            raise SystemExit("unexpected module id")

        for name in names:
            if name.endswith("/"):
                continue
            payload = archive.read(name)
            if payload.startswith(b"\x7fELF"):
                raise SystemExit(f"ELF payload is forbidden: {name}")
            text = payload.decode("utf-8", errors="ignore")
            lowered = text.lower()
            if any(word.lower() in lowered for word in FORBIDDEN_PAYLOAD_WORDS):
                raise SystemExit(f"runtime payload reference is forbidden: {name}")

        for name in ("customize.sh", "run-probe.sh", "service.sh", "action.sh"):
            text = archive.read(name).decode("utf-8")
            if any(word in text for word in MUTATING_SCRIPT_WORDS):
                raise SystemExit(f"camera-mutating operation in {name}")

        result_script = archive.read("run-probe.sh").decode("utf-8")
        for marker in (
            'echo "activation_policy=probe_only"',
            'echo "routing_authorized=false"',
            'echo "camera_mutation_performed=false"',
            'STATE_DIR=/data/adb/android_vcam_capability_probe',
        ):
            if marker not in result_script:
                raise SystemExit(f"missing fail-closed result marker: {marker}")

        uninstall = archive.read("uninstall.sh").decode("utf-8")
        if "/data/adb/android_vcam\n" in uninstall or "/data/adb/modules/android_vcam" in uninstall:
            raise SystemExit("uninstaller may affect the release module")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=pathlib.Path)
    args = parser.parse_args()
    check_archive(args.archive.resolve(strict=True))
    print("Capability probe module archive checks passed")


if __name__ == "__main__":
    main()
