#!/usr/bin/env python3
"""Generate the display version from VERSION without modifying source files."""
import pathlib
import re
import sys

version_path, header_path = map(pathlib.Path, sys.argv[1:])
version = version_path.read_text().strip()
match = re.fullmatch(r"(\d+)\.(\d+)", version)
if not match:
    raise ValueError(f"invalid VERSION: {version!r}")
major, minor = map(int, match.groups())
header = (
    "#pragma once\n"
    f"#define FIRMWARE_VERSION_MAJOR {major}\n"
    f"#define FIRMWARE_VERSION_MINOR {minor}\n"
    f"#define FIRMWARE_BUILD_NUMBER {major * 1000 + minor}\n"
    f'#define FIRMWARE_BUILD_STRING "v {version}"\n'
)
header_path.parent.mkdir(parents=True, exist_ok=True)
if not header_path.exists() or header_path.read_text() != header:
    header_path.write_text(header)
