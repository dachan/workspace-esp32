#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -n "${IDF_EXPORT:-}" ]]; then
  idf_export="$IDF_EXPORT"
elif [[ -n "${IDF_PATH:-}" ]]; then
  idf_export="$IDF_PATH/export.sh"
else
  echo "Set IDF_PATH or IDF_EXPORT before flashing." >&2
  exit 2
fi
if [[ ! -f "$idf_export" ]]; then
  echo "ESP-IDF export script not found: $idf_export" >&2
  exit 2
fi
if [[ -z "${ESP_PORT:-}" ]]; then
  echo "Set ESP_PORT to the serial device for the connected board." >&2
  exit 2
fi
port="$ESP_PORT"

source "$idf_export" >/dev/null
cd "$project_dir"
idf.py -p "$port" build flash
