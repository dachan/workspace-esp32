#!/usr/bin/env python3
"""Bump BUILD_NUMBER when firmware sources change; write build_number.h and archive path stamp."""
from __future__ import annotations

import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT / "main"
BUILD_NUMBER_PATH = ROOT / "BUILD_NUMBER"
STAMP_PATH = ROOT / "build" / ".build_number_stamp"
HEADER_PATH = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else (ROOT / "main" / "build_number.h")
DIST = ROOT / "dist"


def source_fingerprint() -> str:
    h = hashlib.sha256()
    for path in sorted(MAIN.rglob("*")):
        if not path.is_file():
            continue
        if path.name in {"build_number.h"}:
            continue
        if path.suffix not in {".c", ".h", ".yml"} and path.name != "CMakeLists.txt":
            continue
        h.update(path.relative_to(MAIN).as_posix().encode())
        h.update(b"\0")
        h.update(path.read_bytes())
        h.update(b"\0")
    # Project-level cmake / defaults affect the binary too
    for rel in ("CMakeLists.txt", "sdkconfig.defaults"):
        p = ROOT / rel
        if p.is_file():
            h.update(rel.encode())
            h.update(b"\0")
            h.update(p.read_bytes())
            h.update(b"\0")
    return h.hexdigest()


def read_build_number() -> int:
    if BUILD_NUMBER_PATH.is_file():
        text = BUILD_NUMBER_PATH.read_text().strip()
        if text.isdigit():
            return int(text)
    return 0


def write_header(n: int) -> None:
    HEADER_PATH.parent.mkdir(parents=True, exist_ok=True)
    HEADER_PATH.write_text(
        "#pragma once\n"
        f"#define FIRMWARE_BUILD_NUMBER {n}\n"
        f'#define FIRMWARE_BUILD_STRING "b{n}"\n'
    )


def main() -> int:
    fp = source_fingerprint()
    n = read_build_number()
    prev_fp = STAMP_PATH.read_text().strip() if STAMP_PATH.is_file() else ""
    if fp != prev_fp:
        n += 1
        BUILD_NUMBER_PATH.write_text(f"{n}\n")
        STAMP_PATH.parent.mkdir(parents=True, exist_ok=True)
        STAMP_PATH.write_text(fp + "\n")
        print(f"bump_build: sources changed -> build {n}")
    else:
        if n < 1:
            n = 1
            BUILD_NUMBER_PATH.write_text(f"{n}\n")
        print(f"bump_build: unchanged -> build {n}")
    write_header(n)
    DIST.mkdir(parents=True, exist_ok=True)
    (DIST / "CURRENT").write_text(f"{n}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
