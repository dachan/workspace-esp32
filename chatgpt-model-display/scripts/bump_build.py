#!/usr/bin/env python3
"""Bump firmware version (X.Y) when sources change; write build_number.h."""
from __future__ import annotations

import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT / "main"
VERSION_PATH = ROOT / "VERSION"
LEGACY_BUILD_NUMBER = ROOT / "BUILD_NUMBER"
STAMP_PATH = ROOT / "build" / ".build_number_stamp"
HEADER_PATH = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else (ROOT / "main" / "build_number.h")
DIST = ROOT / "dist"


def source_fingerprint() -> str:
    h = hashlib.sha256()
    for path in sorted(MAIN.rglob("*")):
        if not path.is_file():
            continue
        if path.name == "build_number.h":
            continue
        if path.suffix not in {".c", ".h", ".yml"} and path.name != "CMakeLists.txt":
            continue
        h.update(path.relative_to(MAIN).as_posix().encode())
        h.update(b"\0")
        h.update(path.read_bytes())
        h.update(b"\0")
    for rel in ("CMakeLists.txt", "sdkconfig.defaults", "VERSION"):
        p = ROOT / rel
        if p.is_file() and rel != "VERSION":
            h.update(rel.encode())
            h.update(b"\0")
            h.update(p.read_bytes())
            h.update(b"\0")
    return h.hexdigest()


def parse_version(text: str) -> tuple[int, int]:
    m = re.fullmatch(r"(\d+)\.(\d+)", text.strip())
    if not m:
        raise ValueError(f"bad VERSION: {text!r}")
    return int(m.group(1)), int(m.group(2))


def format_version(major: int, minor: int) -> str:
    return f"{major}.{minor}"


def write_text_if_changed(path: pathlib.Path, text: str) -> None:
    if path.is_file() and path.read_text() == text:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def read_version() -> tuple[int, int]:
    if VERSION_PATH.is_file():
        return parse_version(VERSION_PATH.read_text())
    if LEGACY_BUILD_NUMBER.is_file():
        raw = LEGACY_BUILD_NUMBER.read_text().strip()
        if raw.isdigit():
            n = int(raw)
            return 0, n
    return 0, 10


def write_header(major: int, minor: int) -> None:
    ver = format_version(major, minor)
    # Monotonic-ish int for logs: major*1000 + minor
    build_int = major * 1000 + minor
    write_text_if_changed(
        HEADER_PATH,
        "#pragma once\n"
        f"#define FIRMWARE_VERSION_MAJOR {major}\n"
        f"#define FIRMWARE_VERSION_MINOR {minor}\n"
        f"#define FIRMWARE_BUILD_NUMBER {build_int}\n"
        f'#define FIRMWARE_BUILD_STRING "v {ver}"\n'
    )


def main() -> int:
    fp = source_fingerprint()
    major, minor = read_version()
    prev_fp = STAMP_PATH.read_text().strip() if STAMP_PATH.is_file() else ""
    if fp != prev_fp:
        minor += 1
        # First migration: if VERSION was just set to 0.10 and stamp missing,
        # callers may seed VERSION at 0.9 so first bump lands on 0.10.
        ver = format_version(major, minor)
        write_text_if_changed(VERSION_PATH, ver + "\n")
        write_text_if_changed(STAMP_PATH, fp + "\n")
        print(f"bump_build: sources changed -> v {ver}")
    else:
        ver = format_version(major, minor)
        write_text_if_changed(VERSION_PATH, ver + "\n")
        print(f"bump_build: unchanged -> v {ver}")
    write_header(major, minor)
    write_text_if_changed(DIST / "CURRENT", format_version(major, minor) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
