from __future__ import annotations

import argparse
import datetime
import pathlib
import stat
import zipfile


EXECUTABLE_NAMES = {
    "action.sh",
    "boot-completed.sh",
    "customize.sh",
    "post-mount.sh",
    "provider-runner.sh",
    "service.sh",
    "vcamctl",
    "vcam-publisher",
    "vcam-streamer",
    "vcamd",
}


def zip_info(path: pathlib.Path, archive_name: str, is_directory: bool) -> zipfile.ZipInfo:
    modified = datetime.datetime.fromtimestamp(path.stat().st_mtime)
    modified = max(modified, datetime.datetime(1980, 1, 1))
    info = zipfile.ZipInfo(archive_name, modified.timetuple()[:6])
    info.create_system = 3
    permissions = 0o755 if is_directory or path.name in EXECUTABLE_NAMES else 0o644
    file_type = stat.S_IFDIR if is_directory else stat.S_IFREG
    info.external_attr = (file_type | permissions) << 16
    if is_directory:
        info.external_attr |= 0x10
    return info


def main() -> None:
    parser = argparse.ArgumentParser(description="Create an APatch ZIP with explicit POSIX file types")
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    if not source.is_dir():
        raise SystemExit(f"not a directory: {source}")

    with zipfile.ZipFile(args.output, "w") as archive:
        for path in sorted(source.rglob("*"), key=lambda value: value.as_posix()):
            relative = path.relative_to(source).as_posix()
            if path.is_dir():
                relative += "/"
                archive.writestr(zip_info(path, relative, True), b"")
            elif path.is_file():
                info = zip_info(path, relative, False)
                archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED)
            else:
                raise SystemExit(f"unsupported module entry: {path}")


if __name__ == "__main__":
    main()
