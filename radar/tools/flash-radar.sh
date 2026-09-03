#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
role="${1:-transmitter}"
if [[ $# -gt 1 || "$role" != "transmitter" && "$role" != "receiver" ]]; then
  echo "Usage: $0 [transmitter|receiver]"
  exit 2
fi

if [[ "$role" == "transmitter" ]]; then
  build_dir="build-radar-transmitter"
else
  build_dir="build-radar-receiver-accel"
fi

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
idf.py -B "$build_dir" -D "RADAR_LINK_ROLE=$role" build
idf.py -B "$build_dir" -p "$port" app-flash
