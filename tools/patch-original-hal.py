#!/usr/bin/env python3
"""Add the android-vcam proxy dependency to the pinned OEM Camera HAL."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

import lief


PINNED_SHA256 = "dab50dfd0bde9f710c92097442d6451695f7ef82cb9e836b0af2b9369751daa6"
DEFAULT_PROXY_LIBRARY = "libvcam_proxy.so"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--library", default=DEFAULT_PROXY_LIBRARY)
    args = parser.parse_args()

    source = args.input.read_bytes()
    digest = hashlib.sha256(source).hexdigest()
    if digest != PINNED_SHA256:
        raise SystemExit(
            f"refusing unpinned Camera HAL: expected {PINNED_SHA256}, got {digest}"
        )

    binary = lief.parse(source)
    if binary is None:
        raise SystemExit("unable to parse input ELF")
    if args.library not in binary.libraries:
        binary.add_library(args.library)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    binary.write(str(args.output))
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
