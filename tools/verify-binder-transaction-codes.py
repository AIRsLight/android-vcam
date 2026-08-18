#!/usr/bin/env python3
"""Verify Binder transaction constants directly from AArch64 Bp client stubs."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


class VerificationError(RuntimeError):
    pass


def dynamic_symbols(llvm_nm: pathlib.Path, elf: pathlib.Path) -> dict[str, tuple[int, int]]:
    completed = subprocess.run(
        [str(llvm_nm), "-D", "--defined-only", "--format=posix", str(elf)],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    )
    symbols: dict[str, tuple[int, int]] = {}
    for line in completed.stdout.splitlines():
        fields = line.rsplit(maxsplit=3)
        if len(fields) != 4:
            continue
        name, symbol_type, address, size = fields
        if symbol_type.upper() == "T":
            symbols[name] = (int(address, 16), int(size, 16))
    return symbols


def find_bp_symbol(symbols: dict[str, tuple[int, int]], method: str) -> tuple[str, int, int]:
    marker = f"15BpCameraService{len(method)}{method}"
    matches = [(name, *location) for name, location in symbols.items() if marker in name]
    if len(matches) != 1:
        raise VerificationError(
            f"expected one BpCameraService::{method} symbol, found {len(matches)}"
        )
    return matches[0]


def transaction_code(
    llvm_objdump: pathlib.Path, elf: pathlib.Path, symbol: tuple[str, int, int]
) -> int:
    name, address, size = symbol
    completed = subprocess.run(
        [
            str(llvm_objdump),
            "-d",
            f"--start-address=0x{address:x}",
            f"--stop-address=0x{address + size:x}",
            str(elf),
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    )
    instructions = []
    for line in completed.stdout.lower().splitlines():
        match = re.match(r"\s*[0-9a-f]+:\s+[0-9a-f]+\s+(.+)$", line)
        if match:
            instructions.append(match.group(1))
    candidates: set[int] = set()
    for index, instruction in enumerate(instructions):
        match = re.match(r"mov\s+w1,\s+#0x([0-9a-f]+)", instruction)
        if not match:
            continue
        value = int(match.group(1), 16)
        window = instructions[index + 1 : index + 9]
        if 0 < value <= 0xFF and any(
            re.fullmatch(r"mov\s+w4,\s*wzr", candidate) for candidate in window
        ) and any(re.match(r"blr\s+", candidate) for candidate in window):
            candidates.add(value)
    if len(candidates) != 1:
        raise VerificationError(
            f"{name} has ambiguous small W1 immediates: {sorted(candidates)}"
        )
    return candidates.pop()


def recipe_transactions(path: pathlib.Path) -> dict[str, int]:
    transactions: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if len(fields) == 3 and fields[0] == "transaction":
            transactions[fields[1]] = int(fields[2])
    return transactions


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=pathlib.Path)
    parser.add_argument("--llvm-nm", required=True, type=pathlib.Path)
    parser.add_argument("--llvm-objdump", required=True, type=pathlib.Path)
    parser.add_argument(
        "--method", action="append", required=True, metavar="ROLE=CPP_METHOD",
        help="map a recipe role to one BpCameraService C++ method",
    )
    parser.add_argument("--forbid-method", action="append", default=[])
    parser.add_argument("--verify-recipe", required=True, type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    symbols = dynamic_symbols(args.llvm_nm, args.elf)
    expected = recipe_transactions(args.verify_recipe)
    observed: dict[str, int] = {}
    for mapping in args.method:
        if "=" not in mapping or not all(mapping.split("=", 1)):
            raise VerificationError(f"invalid --method value: {mapping}")
        role, method = mapping.split("=", 1)
        if role in observed:
            raise VerificationError(f"duplicate role: {role}")
        observed[role] = transaction_code(
            args.llvm_objdump, args.elf, find_bp_symbol(symbols, method)
        )
    for method in args.forbid_method:
        marker = f"15BpCameraService{len(method)}{method}"
        if any(marker in name for name in symbols):
            raise VerificationError(f"forbidden OEM method is present: {method}")
    for role, code in observed.items():
        if expected.get(role) != code:
            raise VerificationError(
                f"transaction mismatch for {role}: recipe={expected.get(role)} OEM={code}"
            )
        print(f"transaction\t{role}\t{code}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError, VerificationError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
