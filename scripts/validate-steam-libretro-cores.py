#!/usr/bin/env python3
"""Load and inspect every approved libretro core in a Steam runtime."""

from __future__ import annotations

import argparse
import ctypes
from pathlib import Path
import sys


class RetroSystemInfo(ctypes.Structure):
    _fields_ = [
        ("library_name", ctypes.c_char_p),
        ("library_version", ctypes.c_char_p),
        ("valid_extensions", ctypes.c_char_p),
        ("need_fullpath", ctypes.c_bool),
        ("block_extract", ctypes.c_bool),
    ]


def decode(value: bytes | None) -> str:
    return (value or b"").decode("utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("core_directory", type=Path)
    parser.add_argument("approved_inventory", type=Path)
    args = parser.parse_args()

    approved = [
        line.strip()
        for line in args.approved_inventory.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    actual = sorted(path.name for path in args.core_directory.glob("*_libretro.so"))
    if actual != sorted(approved):
        print(
            "Libretro core directory does not match the approved inventory.",
            file=sys.stderr,
        )
        return 1

    failures: list[str] = []
    for core_name in approved:
        core_path = args.core_directory / core_name
        try:
            library = ctypes.CDLL(str(core_path))
            library.retro_api_version.restype = ctypes.c_uint
            api_version = library.retro_api_version()
            if api_version != 1:
                raise RuntimeError(f"reported libretro API {api_version}, expected 1")

            library.retro_get_system_info.argtypes = [
                ctypes.POINTER(RetroSystemInfo)
            ]
            info = RetroSystemInfo()
            library.retro_get_system_info(ctypes.byref(info))
            library_name = decode(info.library_name)
            extensions = decode(info.valid_extensions)
            if not library_name:
                raise RuntimeError("reported an empty library name")
            if not extensions:
                raise RuntimeError("reported no supported content extensions")
        except (AttributeError, OSError, RuntimeError) as error:
            failures.append(f"{core_name}: {error}")

    if failures:
        print("Libretro ABI validation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print(f"Libretro ABI validation passed: {len(approved)} cores")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
