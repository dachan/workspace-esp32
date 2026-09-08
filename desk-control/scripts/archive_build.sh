#!/usr/bin/env bash
set -euo pipefail
BIN="${1:?firmware binary required}"
VERSION_FILE="${2:?VERSION file required}"
DIST="${3:?archive directory required}"
VER="$(tr -d '[:space:]' < "$VERSION_FILE")"
if [[ ! "$VER" =~ ^[0-9]+\.[0-9]+$ || ! -s "$BIN" ]]; then
  echo "archive_build: invalid version or missing firmware binary" >&2
  exit 1
fi
mkdir -p "$DIST"
# Stage each replacement so an interrupted copy cannot truncate the last archive.
cp "$BIN" "$DIST/desk-control-v${VER}.bin.tmp"
mv -f "$DIST/desk-control-v${VER}.bin.tmp" "$DIST/desk-control-v${VER}.bin"
cp "$BIN" "$DIST/desk-control-latest.bin.tmp"
mv -f "$DIST/desk-control-latest.bin.tmp" "$DIST/desk-control-latest.bin"
printf '%s\n' "$VER" > "$DIST/CURRENT.tmp"
mv -f "$DIST/CURRENT.tmp" "$DIST/CURRENT"
echo "archived firmware v${VER}"
