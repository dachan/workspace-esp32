#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
idf_export="$IDF_PATH/export.sh"
role="${1:-transmitter}"
if [[ $# -gt 1 || "$role" != "transmitter" && "$role" != "receiver" ]]; then
  echo "Usage: $0 [transmitter|receiver]"
  exit 2
fi

if [[ "$role" == "transmitter" ]]; then
  build_dir="build-radar-transmitter"
  default_port="/dev/ttyUSB0"
else
  build_dir="build-radar-receiver-accel"
  default_port="/dev/ttyUSB1"
fi
port="${ESP_PORT:-$default_port}"

source "$idf_export" >/dev/null
cd "$project_dir"
idf.py -B "$build_dir" -D "RADAR_LINK_ROLE=$role" build
idf.py -B "$build_dir" -p "$port" app-flash
