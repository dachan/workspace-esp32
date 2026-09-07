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
cp "$BIN" "$DIST/chatgpt-model-display-v${VER}.bin.tmp"
mv -f "$DIST/chatgpt-model-display-v${VER}.bin.tmp" "$DIST/chatgpt-model-display-v${VER}.bin"
cp "$BIN" "$DIST/chatgpt-model-display-latest.bin.tmp"
mv -f "$DIST/chatgpt-model-display-latest.bin.tmp" "$DIST/chatgpt-model-display-latest.bin"
printf '%s\n' "$VER" > "$DIST/CURRENT.tmp"
mv -f "$DIST/CURRENT.tmp" "$DIST/CURRENT"
echo "archived firmware v${VER}"
