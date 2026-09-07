#!/usr/bin/env python3
"""Offline ELF dependency evidence; never loads or executes OEM libraries."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess

from analyze_firmware import elf_defined_dynamic_symbols, elf_identity


def elf_tables(path: pathlib.Path, readelf: str) -> dict:
    output = subprocess.run([readelf, "--wide", "--dyn-syms", "--dynamic", str(path)],
                            check=True, capture_output=True, text=True, timeout=30).stdout
    return parse_elf_tables(output)


def parse_elf_tables(output: str) -> dict:
    needed = re.findall(r"\(NEEDED\).*\[([^\]]+)\]", output)
    exports, imports, weak_imports = set(), set(), set()
    for line in output.splitlines():
        # Older readelf prints AArch64 IFUNC as the multi-word
        # '<OS specific>: 10'. Match the binding column instead of token count.
        match = re.match(r"\s*\d+:\s+\S+\s+\S+\s+.+?\s+"
                         r"(GLOBAL|WEAK|LOCAL|UNIQUE)\s+(\S+)\s+(\S+)\s+(\S+)", line)
        if not match:
            continue
        binding, visibility, section, name = match.groups()
        default_version = "@@" in name
        name = name.replace("@@", "@")
        if section == "UND":
            (weak_imports if binding == "WEAK" else imports).add(name)
        elif binding in ("GLOBAL", "WEAK") and visibility in ("DEFAULT", "PROTECTED"):
            exports.add(name)
            if default_version or "@" not in name:
                exports.add(name.split("@")[0])
    return {"needed": sorted(needed), "exports": exports,
            "imports": imports, "weak_imports": weak_imports}


def audit(sample: pathlib.Path, shim_tables: dict, readelf: str) -> dict:
    root = sample / "partitions"
    module = root / "vendor/lib64/hw/camera.qcom.so"
    slot = module.with_name("local_time.default.so")
    record = {"sample": sample.name, "qualification": "static_only",
              "module_present": module.is_file(), "snapshot_slot_present": slot.is_file()}
    if not module.is_file():
        record["result"] = "unsupported_layout"
        return record
    identity = elf_identity(module, root)
    hmi = elf_defined_dynamic_symbols(module, {"HMI"})
    record["module"] = identity
    record["hmi_size"] = hmi.get("HMI", 0)
    dependencies = {name: [] for name in shim_tables["needed"]}
    root_path = root.resolve()
    for directory, subdirs, files in os.walk(root, followlinks=False):
        subdirs[:] = [name for name in subdirs if name not in
                      ("app", "priv-app", "media", "fonts", "overlay")]
        for name in dependencies.keys() & set(files):
            path = pathlib.Path(directory) / name
            if "lib64" not in path.parts or not path.resolve().is_relative_to(root_path):
                continue
            with path.open("rb") as stream:
                header = stream.read(20)
            if header[:6] == b"\x7fELF\x02\x01" and header[18:20] == b"\xb7\x00":
                dependencies[name].append(path)
    exports = set()
    record["dependency_candidates"] = {}
    for name, paths in sorted(dependencies.items()):
        record["dependency_candidates"][name] = []
        for path in sorted(paths):
            tables = elf_tables(path, readelf)
            exports.update(tables["exports"])
            record["dependency_candidates"][name].append({
                "path": path.relative_to(root).as_posix(),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "matched_imports": sorted(shim_tables["imports"] & tables["exports"]),
            })
    record["missing_dependency_files"] = sorted(name for name, paths in dependencies.items() if not paths)
    record["unresolved_strong_imports"] = sorted(shim_tables["imports"] - exports)
    record["unresolved_weak_imports"] = sorted(shim_tables["weak_imports"] - exports)
    valid_module = identity["architecture"] == "arm64" and hmi.get("HMI") == 344
    record["result"] = (
        "unsupported_module" if not valid_module or not slot.is_file() else
        "incomplete_dependency_evidence" if record["missing_dependency_files"] or
        record["unresolved_strong_imports"] else "static_dependency_candidates_present"
    )
    return record


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cohort-root", type=pathlib.Path, required=True)
    parser.add_argument("--shim", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--readelf", default="readelf")
    args = parser.parse_args()
    shim_identity = elf_identity(args.shim, args.shim.parent)
    if (shim_identity["architecture"] != "arm64" or
            elf_defined_dynamic_symbols(args.shim, {"HMI"}).get("HMI") != 344):
        parser.error("shim must be AArch64 with a visible 344-byte HMI OBJECT")
    tables = elf_tables(args.shim, args.readelf)
    report = {
        "schema": 1, "shim_sha256": hashlib.sha256(args.shim.read_bytes()).hexdigest(),
        "shim_hmi_size": elf_defined_dynamic_symbols(args.shim, {"HMI"}).get("HMI", 0),
        "shim_needed": tables["needed"], "strong_import_count": len(tables["imports"]),
        "limitations": [
            "Matches are the union of ELF exports across candidate dependency paths, not a resolved linker namespace.",
            "Missing APEX extraction can leave platform dependencies inconclusive.",
            "Does not execute HAL init/open, validate relocation or SONAME behavior, gralloc/fences, SELinux, boot or frames.",
            "Only this Qualcomm five-firmware cohort is covered; other OnePlus chipsets and ROMs are not certified.",
        ],
        "samples": [audit(sample, tables, args.readelf) for sample in sorted(args.cohort_root.iterdir())
                    if (sample / "partitions").is_dir()],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for sample in report["samples"]:
        print(sample["sample"], sample["result"],
              "missing=" + ",".join(sample.get("missing_dependency_files", [])),
              "unresolved=" + str(len(sample.get("unresolved_strong_imports", []))))


if __name__ == "__main__":
    main()
