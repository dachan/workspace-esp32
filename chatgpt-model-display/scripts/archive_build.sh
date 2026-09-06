#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="$(tr -d '[:space:]' < "$ROOT/VERSION")"
BIN="$ROOT/build/chatgpt-model-display.bin"
DEST="$ROOT/dist/chatgpt-model-display-v${VER}.bin"
mkdir -p "$ROOT/dist"
if [[ -f "$BIN" ]]; then
  cp -f "$BIN" "$DEST"
  cp -f "$BIN" "$ROOT/dist/chatgpt-model-display-latest.bin"
  echo "archived $DEST"
else
  echo "archive_build: missing $BIN" >&2
  exit 1
fi
