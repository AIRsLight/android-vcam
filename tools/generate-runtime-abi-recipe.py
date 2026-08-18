#!/usr/bin/env python3
"""Generate a strict runtime ABI recipe from a 64-bit little-endian ELF library."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import subprocess
import sys


PT_LOAD = 1
PT_NOTE = 4
NT_GNU_BUILD_ID = 3
EM_AARCH64 = 183


class ElfError(RuntimeError):
    pass


def align4(value: int) -> int:
    return (value + 3) & ~3


def read_program_headers(data: bytes) -> list[tuple[int, int, int, int, int, int, int, int]]:
    if len(data) < 64 or data[:4] != b"\x7fELF":
        raise ElfError("input is not an ELF file")
    if data[4] != 2 or data[5] != 1:
        raise ElfError("only ELF64 little-endian files are supported")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
    program_offset = header[5]
    entry_size = header[9]
    entry_count = header[10]
    if entry_size < 56 or program_offset + entry_size * entry_count > len(data):
        raise ElfError("invalid program header table")
    return [
        struct.unpack_from("<IIQQQQQQ", data, program_offset + index * entry_size)
        for index in range(entry_count)
    ]


def read_build_id(data: bytes, headers: list[tuple[int, ...]]) -> bytes:
    for header in headers:
        segment_type, _, file_offset, _, _, file_size, _, _ = header
        if segment_type != PT_NOTE:
            continue
        cursor = file_offset
        end = file_offset + file_size
        while cursor + 12 <= end:
            name_size, descriptor_size, note_type = struct.unpack_from("<III", data, cursor)
            cursor += 12
            name = data[cursor : cursor + name_size]
            cursor += align4(name_size)
            descriptor = data[cursor : cursor + descriptor_size]
            cursor += align4(descriptor_size)
            if cursor > end:
                break
            if note_type == NT_GNU_BUILD_ID and name.rstrip(b"\0") == b"GNU":
                return descriptor
    raise ElfError("ELF has no GNU Build ID")


def read_dynamic_symbols(llvm_nm: pathlib.Path, elf_path: pathlib.Path) -> dict[str, int]:
    completed = subprocess.run(
        [str(llvm_nm), "-D", "--defined-only", "--format=posix", str(elf_path)],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="strict",
    )
    symbols: dict[str, int] = {}
    for line in completed.stdout.splitlines():
        fields = line.rsplit(maxsplit=3)
        if len(fields) != 4:
            continue
        name, symbol_type, address, _ = fields
        if symbol_type.upper() in {"T", "W"}:
            symbols[name] = int(address, 16)
    return symbols


def code_prefix(data: bytes, headers: list[tuple[int, ...]], address: int, size: int) -> bytes:
    for header in headers:
        segment_type, _, file_offset, virtual_address, _, file_size, _, _ = header
        if segment_type != PT_LOAD:
            continue
        relative = address - virtual_address
        if relative >= 0 and relative + size <= file_size:
            return data[file_offset + relative : file_offset + relative + size]
    raise ElfError(f"symbol address 0x{address:x} is not backed by a loadable file segment")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=pathlib.Path)
    parser.add_argument("--llvm-nm", required=True, type=pathlib.Path)
    parser.add_argument("--module", help="loaded module suffix; defaults to the ELF file name")
    parser.add_argument("--symbol", action="append", default=[], help="exact dynamic symbol name")
    parser.add_argument("--architecture", choices=["arm64"])
    parser.add_argument(
        "--hook", action="append", default=[], metavar="ROLE=SYMBOL",
        help="bind a strategy hook role to an exact dynamic symbol",
    )
    parser.add_argument(
        "--transaction", action="append", default=[], metavar="ROLE=CODE",
        help="bind a Binder transaction role to its positive decimal code",
    )
    parser.add_argument("--prefix-bytes", type=int, default=16)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 4 <= args.prefix_bytes <= 64:
        raise ElfError("--prefix-bytes must be between 4 and 64")
    hooks: list[tuple[str, str]] = []
    for value in args.hook:
        if "=" not in value or not all(value.split("=", 1)):
            raise ElfError(f"invalid --hook value: {value}")
        hooks.append(tuple(value.split("=", 1)))
    transactions: list[tuple[str, int]] = []
    for value in args.transaction:
        if "=" not in value or not all(value.split("=", 1)):
            raise ElfError(f"invalid --transaction value: {value}")
        role, code_text = value.split("=", 1)
        if not code_text.isdecimal() or int(code_text) <= 0 or int(code_text) > 0xFFFFFFFF:
            raise ElfError(f"invalid Binder transaction code: {value}")
        transactions.append((role, int(code_text)))
    strategy_requested = bool(args.architecture or hooks or transactions)
    if strategy_requested and not (args.architecture and hooks and transactions):
        raise ElfError("strategy recipes require --architecture, --hook and --transaction")
    if len({role for role, _ in hooks}) != len(hooks):
        raise ElfError("hook roles must be unique")
    if len({role for role, _ in transactions}) != len(transactions) or \
            len({code for _, code in transactions}) != len(transactions):
        raise ElfError("transaction roles and codes must be unique")

    requested_symbols = list(dict.fromkeys(args.symbol + [symbol for _, symbol in hooks]))
    if not requested_symbols:
        raise ElfError("at least one --symbol or --hook is required")

    data = args.elf.read_bytes()
    headers = read_program_headers(data)
    machine = struct.unpack_from("<H", data, 18)[0] if len(data) >= 20 else 0
    if args.architecture == "arm64" and machine != EM_AARCH64:
        raise ElfError("--architecture arm64 requires an EM_AARCH64 ELF input")
    build_id = read_build_id(data, headers)
    dynamic_symbols = read_dynamic_symbols(args.llvm_nm, args.elf)

    requirements: list[tuple[str, bytes]] = []
    for name in requested_symbols:
        if name not in dynamic_symbols:
            raise ElfError(f"required dynamic code symbol is missing: {name}")
        requirements.append(
            (name, code_prefix(data, headers, dynamic_symbols[name], args.prefix_bytes))
        )

    lines = [
        "# Generated by tools/generate-runtime-abi-recipe.py; unknown builds must fail closed.",
        f"schema\t{2 if strategy_requested else 1}",
        *([f"architecture\t{args.architecture}"] if strategy_requested else []),
        f"module\t{args.module or args.elf.name}",
        f"file_size\t{len(data)}",
        f"sha256\t{hashlib.sha256(data).hexdigest()}",
        f"build_id\t{build_id.hex()}",
    ]
    lines.extend(f"symbol\t{name}\t{prefix.hex()}" for name, prefix in requirements)
    lines.extend(f"hook\t{role}\t{symbol}" for role, symbol in hooks)
    lines.extend(f"transaction\t{role}\t{code}" for role, code in transactions)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ElfError, OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
