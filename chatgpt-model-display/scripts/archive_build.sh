#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
N="$(tr -d '[:space:]' < "$ROOT/BUILD_NUMBER")"
BIN="$ROOT/build/chatgpt-model-display.bin"
DEST="$ROOT/dist/chatgpt-model-display-b${N}.bin"
mkdir -p "$ROOT/dist"
if [[ -f "$BIN" ]]; then
  cp -f "$BIN" "$DEST"
  cp -f "$BIN" "$ROOT/dist/chatgpt-model-display-latest.bin"
  echo "archived $DEST"
else
  echo "archive_build: missing $BIN" >&2
  exit 1
fi
