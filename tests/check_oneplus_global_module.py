#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import zipfile


REQUIRED = {
    "module.prop",
    "customize.sh",
    "post-mount.sh",
    "service.sh",
    "sepolicy.rule",
    "system/vendor/lib64/hw/camera.qcom.so",
    "system/vendor/bin/vcam-publisher",
    "system/bin/vcam-streamer",
    "system/bin/vcamd",
    "system/framework/vcam-https-downloader.jar",
}


def fail(message: str) -> None:
    raise SystemExit(message)


def check_elf64_arm64(payload: bytes, name: str) -> None:
    if len(payload) < 20 or payload[:6] != b"\x7fELF\x02\x01":
        fail(f"not a little-endian ELF64 file: {name}")
    if struct.unpack_from("<H", payload, 18)[0] != 183:
        fail(f"not an AArch64 file: {name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive")
    args = parser.parse_args()
    with zipfile.ZipFile(args.archive) as archive:
        names = set(archive.namelist())
        missing = REQUIRED - names
        if missing:
            fail("missing OnePlus module files: " + ", ".join(sorted(missing)))
        if "system/vendor/lib64/hw/local_time.default.so" in names:
            fail("release archive must not contain the proprietary OEM HAL snapshot")
        module_prop = archive.read("module.prop").decode("utf-8")
        customize = archive.read("customize.sh").decode("utf-8")
        if "id=android_vcam_oneplus_global" not in module_prop:
            fail("unexpected module ID")
        for marker in (
            "29|30|31|32|33|34",
            "original_camera_hal_sha256",
            "route_scope=global",
            "META_ROOT=/data/adb/metamodule",
        ):
            if marker not in customize:
                fail(f"installer safety marker missing: {marker}")
        for name in (
            "system/vendor/lib64/hw/camera.qcom.so",
            "system/vendor/bin/vcam-publisher",
            "system/bin/vcam-streamer",
            "system/bin/vcamd",
        ):
            check_elf64_arm64(archive.read(name), name)
        shim = archive.read("system/vendor/lib64/hw/camera.qcom.so")
        if b"HMI\0" not in shim or b"camera.qcom.so\0" not in shim:
            fail("camera shim does not export the expected Camera Module identity")
    print("OnePlus global module archive checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
