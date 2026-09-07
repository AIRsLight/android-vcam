#!/usr/bin/env python3
"""Build a reproducible Camera-stack signature from extracted Android firmware."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import struct
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from typing import Any, Iterable


CAMERA_CORE_NAMES = {
    "cameraserver",
    "libcamera_client.so",
    "libcamera_metadata.so",
    "libcameraservice.so",
}
PARTITIONS = (
    "system", "system_ext", "product", "vendor", "odm",
    # OPlus/ColorOS-derived firmware moves overlays and product contracts into
    # these dynamic partitions. They are small enough to include in static
    # compatibility discovery without extracting media/app payload partitions.
    "my_product", "my_manifest", "my_region", "my_carrier",
)
TEXT_LIMIT = 16 * 1024 * 1024
ELF_MAGIC = b"\x7fELF"
PT_NOTE = 4
NT_GNU_BUILD_ID = 3
SHT_DYNSYM = 11
SHN_UNDEF = 0


class AnalysisError(RuntimeError):
    pass


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def relative(path: pathlib.Path, root: pathlib.Path) -> str:
    return path.relative_to(root).as_posix()


def read_text(path: pathlib.Path) -> str:
    if path.stat().st_size > TEXT_LIMIT:
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def iter_files(root: pathlib.Path) -> Iterable[pathlib.Path]:
    for directory, names, filenames in os.walk(root):
        names.sort()
        for filename in sorted(filenames):
            path = pathlib.Path(directory) / filename
            # Partition images commonly contain absolute Android symlinks. They
            # are not meaningful from the host extraction root and may resolve
            # to a nonexistent host path, so analyse their real target file
            # instead of following the link here.
            if path.is_symlink():
                continue
            yield path


def discover_partition_roots(root: pathlib.Path) -> dict[str, pathlib.Path]:
    found: dict[str, pathlib.Path] = {}
    candidates = [root, root / "extracted", root / "partitions"]
    for candidate in candidates:
        for partition in PARTITIONS:
            path = candidate / partition
            if path.is_dir() and partition not in found:
                found[partition] = path
    if root.name in PARTITIONS and root.is_dir():
        found[root.name] = root
    return found


def parse_properties(paths: Iterable[pathlib.Path]) -> dict[str, str]:
    properties: dict[str, str] = {}
    for path in sorted(paths):
        for line in read_text(path).splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            properties.setdefault(key.strip(), value.strip())
    return properties


def xml_name(element: ET.Element) -> str:
    return element.tag.rsplit("}", 1)[-1]


def child_text(element: ET.Element, name: str) -> str:
    for child in element:
        if xml_name(child) == name:
            return (child.text or "").strip()
    return ""


def parse_vintf(path: pathlib.Path, root: pathlib.Path) -> dict[str, Any]:
    record: dict[str, Any] = {
        "path": relative(path, root),
        "document_type": "unknown",
        "target_level": "",
        "camera_hals": [],
        "parse_error": "",
    }
    try:
        document = ET.fromstring(read_text(path))
    except (ET.ParseError, OSError) as error:
        record["parse_error"] = str(error)
        return record
    record["document_type"] = xml_name(document)
    record["target_level"] = document.attrib.get("target-level", "")
    for hal in document.iter():
        if xml_name(hal) != "hal":
            continue
        name = child_text(hal, "name")
        if "camera" not in name.lower():
            continue
        interfaces = []
        for interface in hal:
            if xml_name(interface) != "interface":
                continue
            interface_name = child_text(interface, "name")
            instances = [
                (entry.text or "").strip()
                for entry in interface
                if xml_name(entry) in {"instance", "regex-instance"}
            ]
            interfaces.append({"name": interface_name, "instances": sorted(filter(None, instances))})
        fqnames = sorted(
            (entry.text or "").strip()
            for entry in hal
            if xml_name(entry) == "fqname" and (entry.text or "").strip()
        )
        versions = sorted(
            (entry.text or "").strip()
            for entry in hal
            if xml_name(entry) == "version" and (entry.text or "").strip()
        )
        record["camera_hals"].append(
            {
                "name": name,
                "format": hal.attrib.get("format", "hidl"),
                "transport": child_text(hal, "transport"),
                "versions": versions,
                "fqnames": fqnames,
                "interfaces": interfaces,
            }
        )
    return record


def align4(value: int) -> int:
    return (value + 3) & ~3


def elf_identity(path: pathlib.Path, root: pathlib.Path) -> dict[str, Any]:
    record: dict[str, Any] = {
        "path": relative(path, root),
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
        "build_id": "",
        "machine": "",
        "bits": 0,
        "architecture": "unknown",
    }
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) < 64 or header[:4] != ELF_MAGIC:
            return record
        elf_class, endian = header[4], header[5]
        if endian != 1 or elf_class not in (1, 2):
            return record
        machine = struct.unpack_from("<H", header, 18)[0]
        record["machine"] = str(machine)
        record["bits"] = 64 if elf_class == 2 else 32
        record["architecture"] = {3: "x86", 40: "arm", 62: "x86_64", 183: "arm64"}.get(
            machine, f"elf-machine-{machine}"
        )
        if elf_class == 2:
            program_offset = struct.unpack_from("<Q", header, 32)[0]
            entry_size = struct.unpack_from("<H", header, 54)[0]
            entry_count = struct.unpack_from("<H", header, 56)[0]
            fmt, minimum = "<IIQQQQQQ", 56
        else:
            program_offset = struct.unpack_from("<I", header, 28)[0]
            entry_size = struct.unpack_from("<H", header, 42)[0]
            entry_count = struct.unpack_from("<H", header, 44)[0]
            fmt, minimum = "<IIIIIIII", 32
        if entry_size < minimum or entry_count > 4096:
            return record
        for index in range(entry_count):
            stream.seek(program_offset + index * entry_size)
            item = stream.read(minimum)
            if len(item) != minimum:
                break
            fields = struct.unpack(fmt, item)
            if fields[0] != PT_NOTE:
                continue
            if elf_class == 2:
                file_offset, file_size = fields[2], fields[5]
            else:
                file_offset, file_size = fields[1], fields[4]
            if file_size > 16 * 1024 * 1024:
                continue
            stream.seek(file_offset)
            notes = stream.read(file_size)
            cursor = 0
            while cursor + 12 <= len(notes):
                name_size, descriptor_size, note_type = struct.unpack_from("<III", notes, cursor)
                cursor += 12
                name = notes[cursor : cursor + name_size]
                cursor += align4(name_size)
                descriptor = notes[cursor : cursor + descriptor_size]
                cursor += align4(descriptor_size)
                if cursor > len(notes):
                    break
                if note_type == NT_GNU_BUILD_ID and name.rstrip(b"\0") == b"GNU":
                    record["build_id"] = descriptor.hex()
                    return record
    return record


def elf_defined_dynamic_symbols(path: pathlib.Path, wanted: set[str]) -> dict[str, int]:
    """Return wanted defined ELF dynamic symbols and their declared byte sizes."""
    if not wanted:
        return {}
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) < 52 or header[:4] != ELF_MAGIC or header[5] != 1:
            return {}
        elf_class = header[4]
        if elf_class == 2:
            if len(header) < 64:
                return {}
            section_offset = struct.unpack_from("<Q", header, 40)[0]
            section_entry_size = struct.unpack_from("<H", header, 58)[0]
            section_count = struct.unpack_from("<H", header, 60)[0]
            section_format, minimum = "<IIQQQQIIQQ", 64
            symbol_format, symbol_minimum = "<IBBHQQ", 24
        elif elf_class == 1:
            section_offset = struct.unpack_from("<I", header, 32)[0]
            section_entry_size = struct.unpack_from("<H", header, 46)[0]
            section_count = struct.unpack_from("<H", header, 48)[0]
            section_format, minimum = "<IIIIIIIIII", 40
            symbol_format, symbol_minimum = "<IIIBBH", 16
        else:
            return {}
        if section_entry_size < minimum or section_count == 0 or section_count > 65535:
            return {}
        file_size = path.stat().st_size
        if section_offset > file_size or section_entry_size * section_count > file_size - section_offset:
            return {}
        sections: list[tuple[int, ...]] = []
        for index in range(section_count):
            stream.seek(section_offset + index * section_entry_size)
            payload = stream.read(minimum)
            if len(payload) != minimum:
                return {}
            sections.append(struct.unpack(section_format, payload))

        found: dict[str, int] = {}
        for section in sections:
            if section[1] != SHT_DYNSYM:
                continue
            offset, size, string_index, entry_size = section[4], section[5], section[6], section[9]
            if (string_index >= len(sections) or entry_size < symbol_minimum or
                    size % entry_size != 0 or
                    offset > file_size or size > file_size - offset):
                continue
            strings_section = sections[string_index]
            strings_offset, strings_size = strings_section[4], strings_section[5]
            if (strings_section[1] != 3 or strings_size > TEXT_LIMIT or
                    strings_offset > file_size or strings_size > file_size - strings_offset):
                continue
            stream.seek(strings_offset)
            strings = stream.read(strings_size)
            for cursor in range(offset, offset + size, entry_size):
                stream.seek(cursor)
                payload = stream.read(symbol_minimum)
                if len(payload) != symbol_minimum:
                    break
                fields = struct.unpack(symbol_format, payload)
                if elf_class == 2:
                    name_offset, info, visibility, section_index, _, symbol_size = fields
                else:
                    name_offset, _, symbol_size, info, visibility, section_index = fields
                if (section_index == SHN_UNDEF or (info >> 4) not in (1, 2) or
                        (visibility & 3) not in (0, 3) or name_offset >= len(strings)):
                    continue
                terminator = strings.find(b"\0", name_offset)
                if terminator < 0:
                    continue
                name = strings[name_offset:terminator].decode("ascii", errors="replace")
                if name in wanted:
                    if name == "HMI" and (info & 15) != 1:
                        continue
                    found[name] = symbol_size
            if set(found) == wanted:
                break
        return found


def is_camera_elf(path: pathlib.Path) -> bool:
    lowered = path.as_posix().lower()
    name = path.name.lower()
    if name in CAMERA_CORE_NAMES:
        return True
    if not (name.endswith(".so") or "/bin/" in lowered or "/bin/hw/" in lowered):
        return False
    return "camera" in name or "camera" in lowered and ("provider" in lowered or "/hw/" in lowered)


def parse_init_services(path: pathlib.Path, root: pathlib.Path) -> list[dict[str, Any]]:
    lines = read_text(path).splitlines()
    services: list[dict[str, Any]] = []
    current: list[str] = []

    def flush() -> None:
        nonlocal current
        # Keep services whose identity is camera-specific. Many unrelated
        # Android daemons belong to the `camera` Unix group, so matching the
        # entire stanza makes the compatibility signature noisy.
        if current and "camera" in current[0].lower():
            fields = current[0].split()
            services.append(
                {
                    "path": relative(path, root),
                    "name": fields[1] if len(fields) > 1 else "",
                    "command": " ".join(fields[2:]),
                    "body": [entry.strip() for entry in current[1:] if entry.strip()],
                }
            )
        current = []

    for line in lines:
        if line.startswith("service "):
            flush()
            current = [line]
        elif current:
            if line.startswith((" ", "\t")):
                current.append(line)
            else:
                # A service stanza only owns indented option lines. Top-level
                # comments and blank lines delimit it just like another rc
                # directive; retaining them can associate a later camera
                # comment with an unrelated service.
                flush()
    flush()
    return services


def stable_unique(values: Iterable[str]) -> list[str]:
    return sorted(set(filter(None, values)))


def stable_unique_records(records: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    by_json = {
        json.dumps(record, sort_keys=True, separators=(",", ":")): record
        for record in records
    }
    return [by_json[key] for key in sorted(by_json)]


def first_property(properties: dict[str, str], *keys: str) -> str:
    return next((properties[key] for key in keys if properties.get(key)), "")


def analyze(root: pathlib.Path, label: str) -> dict[str, Any]:
    root = root.resolve()
    if not root.is_dir():
        raise AnalysisError(f"extracted firmware directory does not exist: {root}")
    partition_roots = discover_partition_roots(root)
    if not partition_roots:
        raise AnalysisError("no extracted system/vendor/product/odm partition directories found")

    files: list[pathlib.Path] = []
    for partition_root in partition_roots.values():
        files.extend(iter_files(partition_root))
    property_paths = [path for path in files if path.name in {"build.prop", "default.prop"}]
    properties = parse_properties(property_paths)
    vintf_paths = [
        path for path in files
        if path.suffix == ".xml" and "/etc/vintf/" in path.as_posix().lower()
    ]
    vintf = [parse_vintf(path, root) for path in sorted(vintf_paths)]
    init_paths = [path for path in files if path.suffix == ".rc" and "/etc/init/" in path.as_posix().lower()]
    services = []
    for path in sorted(init_paths):
        services.extend(parse_init_services(path, root))

    context_names = {
        "file_contexts", "file_contexts.bin", "hwservice_contexts", "plat_hwservice_contexts",
        "plat_service_contexts", "service_contexts", "vendor_hwservice_contexts",
        "vendor_service_contexts", "vndservice_contexts",
    }
    policy_evidence = []
    for path in files:
        if path.name not in context_names and path.suffix != ".cil":
            continue
        text = read_text(path)
        matches = stable_unique(line.strip() for line in text.splitlines() if "camera" in line.lower())
        if matches:
            policy_evidence.append({"path": relative(path, root), "camera_lines": matches[:200]})

    elf_paths = [path for path in files if is_camera_elf(path)]
    binaries = [elf_identity(path, root) for path in sorted(elf_paths)]
    legacy_camera_modules = []
    for path in sorted(files):
        lowered = path.as_posix().lower()
        if not re.search(r"/lib64/hw/camera\.[^/]+\.so$", lowered):
            continue
        identity = elf_identity(path, root)
        exports = elf_defined_dynamic_symbols(path, {"HMI"})
        identity["exports_hmi"] = "HMI" in exports
        identity["hmi_symbol_size"] = exports.get("HMI", 0)
        identity["portable_global_shim_candidate"] = (
            identity["architecture"] == "arm64" and identity["bits"] == 64 and
            identity["exports_hmi"] and identity["hmi_symbol_size"] == 344 and
            identity["size"] > 65536
        )
        legacy_camera_modules.append(identity)

    camera_hals = [hal for record in vintf for hal in record["camera_hals"]]
    provided_hals = [
        hal for record in vintf if record["document_type"] == "manifest"
        for hal in record["camera_hals"]
    ]
    required_hals = [
        hal for record in vintf if record["document_type"] == "compatibility-matrix"
        for hal in record["camera_hals"]
    ]
    provider_hals = [hal for hal in provided_hals if hal["name"] == "android.hardware.camera.provider"]
    formats = stable_unique(hal["format"] for hal in provider_hals or provided_hals or camera_hals)
    transport = "+".join(formats) if formats else "unknown"
    target_levels = stable_unique(record["target_level"] for record in vintf)
    sdk = first_property(
        properties, "ro.build.version.sdk", "ro.system.build.version.sdk",
        "ro.product.build.version.sdk", "ro.vendor.build.version.sdk",
    )
    release = first_property(
        properties, "ro.build.version.release", "ro.system.build.version.release",
        "ro.product.build.version.release", "ro.vendor.build.version.release",
    )
    fingerprint = first_property(
        properties, "ro.build.fingerprint", "ro.product.build.fingerprint",
        "ro.system.build.fingerprint", "ro.vendor.build.fingerprint",
    )
    core_ids = {
        item["path"]: {
            "identity": item["build_id"] or item["sha256"][:24],
            "architecture": item["architecture"],
            "bits": item["bits"],
        }
        for item in binaries
        if pathlib.PurePosixPath(item["path"]).name in CAMERA_CORE_NAMES
    }
    compact_hal = lambda hal: {
        "name": hal["name"], "format": hal["format"], "transport": hal["transport"],
        "versions": hal["versions"], "fqnames": hal["fqnames"], "interfaces": hal["interfaces"],
    }
    signature_source = {
        "sdk": sdk,
        "release": release,
        "target_levels": target_levels,
        "provider_transport": transport,
        "provided_camera_hals": stable_unique_records(compact_hal(hal) for hal in provided_hals),
        "required_camera_hals": stable_unique_records(compact_hal(hal) for hal in required_hals),
        "camera_services": [{"name": item["name"], "command": item["command"]} for item in services],
        "core_build_ids": core_ids,
    }
    signature_json = json.dumps(signature_source, sort_keys=True, separators=(",", ":"))
    signature = hashlib.sha256(signature_json.encode("utf-8")).hexdigest()
    return {
        "schema": 1,
        "label": label,
        "analysis_level": "S1-static",
        "source_root": str(root),
        "partitions": {name: relative(path, root) or "." for name, path in sorted(partition_roots.items())},
        "build": {
            "release": release,
            "sdk": sdk,
            "fingerprint": fingerprint,
            "product": first_property(
                properties, "ro.product.name", "ro.product.product.name",
                "ro.product.system.name", "ro.product.vendor.name",
            ),
            "device": first_property(
                properties, "ro.product.device", "ro.product.product.device",
                "ro.product.system.device", "ro.product.vendor.device",
            ),
            "manufacturer": first_property(
                properties, "ro.product.manufacturer", "ro.product.product.manufacturer",
                "ro.product.system.manufacturer", "ro.product.vendor.manufacturer",
            ),
            "security_patch": properties.get("ro.build.version.security_patch", ""),
        },
        "compatibility_signature": signature,
        "signature_inputs": signature_source,
        "vintf": vintf,
        "camera_services": services,
        "selinux_camera_evidence": policy_evidence,
        "camera_binaries": binaries,
        "legacy_camera_modules": legacy_camera_modules,
        "limitations": [
            "Static firmware cannot prove provider registration or frame delivery on hardware.",
            "Runtime camera IDs, caller attribution, buffer/fence behavior and resource conflicts are not observed.",
            "Vendor-tag presence can be inventoried, but proprietary semantics cannot be inferred safely.",
        ],
    }


def nested_get(value: dict[str, Any], path: str) -> Any:
    current: Any = value
    for part in path.split("."):
        if not isinstance(current, dict):
            return None
        current = current.get(part)
    return current


def compare(current: dict[str, Any], baseline: dict[str, Any]) -> dict[str, Any]:
    fields = (
        "build.release", "build.sdk", "build.product", "build.device",
        "signature_inputs.target_levels", "signature_inputs.provider_transport",
        "signature_inputs.provided_camera_hals", "signature_inputs.required_camera_hals",
        "signature_inputs.camera_services",
        "signature_inputs.core_build_ids",
    )
    changes = []
    for field in fields:
        before, after = nested_get(baseline, field), nested_get(current, field)
        if before != after:
            changes.append({"field": field, "baseline": before, "current": after})
    high_risk_fields = {
        "signature_inputs.provider_transport",
        "signature_inputs.provided_camera_hals",
        "signature_inputs.required_camera_hals",
        "signature_inputs.camera_services",
        "signature_inputs.core_build_ids",
    }
    risk = "high" if any(item["field"] in high_risk_fields for item in changes) else "low"
    if not changes:
        risk = "same-signature-inputs"
    return {
        "baseline_label": baseline.get("label", ""),
        "same_signature": baseline.get("compatibility_signature") == current.get("compatibility_signature"),
        "difference_risk": risk,
        "changes": changes,
    }


def markdown(report: dict[str, Any]) -> str:
    build = report["build"]
    lines = [
        f"# Firmware camera compatibility report: {report['label']}", "",
        f"- Analysis level: `{report['analysis_level']}`",
        f"- Android: `{build['release'] or 'unknown'}` / SDK `{build['sdk'] or 'unknown'}`",
        f"- Product/device: `{build['product'] or 'unknown'}` / `{build['device'] or 'unknown'}`",
        f"- Fingerprint: `{build['fingerprint'] or 'unknown'}`",
        f"- Compatibility signature: `{report['compatibility_signature']}`", "",
        "## Camera interface", "",
        f"Provider transport family: `{report['signature_inputs']['provider_transport']}`", "",
        "### Provided by device manifests", "",
    ]
    for record in report["vintf"]:
        if record["document_type"] != "manifest":
            continue
        for hal in record["camera_hals"]:
            lines.append(
                f"- `{hal['name']}` format `{hal['format']}` versions "
                f"`{','.join(hal['versions']) or 'unspecified'}` in `{record['path']}`"
            )
    lines.extend(["", "### Framework compatibility requirements", ""])
    for record in report["vintf"]:
        if record["document_type"] != "compatibility-matrix":
            continue
        for hal in record["camera_hals"]:
            versions = ", ".join(hal["versions"] or hal["fqnames"] or ["unspecified"])
            lines.append(
                f"- `{hal['name']}` format `{hal['format']}` versions `{versions}` "
                f"in `{record['path']}`"
            )
    lines.extend(["", "## Camera services", ""])
    for service in report["camera_services"]:
        lines.append(f"- `{service['name']}`: `{service['command']}` (`{service['path']}`)")
    lines.extend(["", "## Core binaries", ""])
    for binary in report["camera_binaries"]:
        if pathlib.PurePosixPath(binary["path"]).name in CAMERA_CORE_NAMES:
            lines.append(
                f"- `{binary['path']}` — `{binary['architecture']}`/{binary['bits']}-bit, "
                f"build ID `{binary['build_id'] or 'missing'}`, "
                f"SHA-256 `{binary['sha256']}`"
            )
    if "comparison" in report:
        comparison = report["comparison"]
        lines.extend([
            "", "## Baseline comparison", "",
            f"- Baseline: `{comparison['baseline_label']}`",
            f"- Same signature: `{str(comparison['same_signature']).lower()}`",
            f"- Difference risk: `{comparison['difference_risk']}`", "",
        ])
        for item in comparison["changes"]:
            lines.append(f"- Changed `{item['field']}`")
    lines.extend([
        "", "## Boundary", "",
        "This is an S1 static result. It does not qualify routing, camera opening, frame output,",
        "caller attribution, third-party applications, or boot stability on real hardware.", "",
    ])
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=pathlib.Path, help="directory containing extracted partition trees")
    parser.add_argument("--label", required=True)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--markdown", type=pathlib.Path)
    parser.add_argument("--compare", type=pathlib.Path, help="baseline JSON report")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = analyze(args.root, args.label)
    if args.compare:
        baseline = json.loads(args.compare.read_text(encoding="utf-8"))
        report["comparison"] = compare(report, baseline)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(markdown(report), encoding="utf-8", newline="\n")
    print(report["compatibility_signature"])
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AnalysisError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
