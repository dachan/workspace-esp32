#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
role="${1:-transmitter}"
board="default"

if [[ "$role" == "transmitter-supermini" ]]; then
  role="transmitter"
  board="supermini"
elif [[ $# -ge 2 ]]; then
  board="$2"
fi

if [[ $# -gt 2 \
   || ( "$role" != "transmitter" && "$role" != "receiver" ) \
   || ( "$board" != "default" && "$board" != "supermini" ) \
   || ( "$board" == "supermini" && "$role" != "transmitter" ) ]]; then
  echo "Usage: $0 [transmitter|receiver|transmitter-supermini]"
  echo "       $0 transmitter supermini"
  exit 2
fi

if [[ "$role" == "transmitter" && "$board" == "supermini" ]]; then
  build_dir="build-radar-transmitter-supermini"
elif [[ "$role" == "transmitter" ]]; then
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

cmake_args=(-B "$build_dir" -D "RADAR_LINK_ROLE=$role")
if [[ "$board" == "supermini" ]]; then
  cmake_args+=(
    -D RADAR_BOARD=supermini
    -D "SDKCONFIG=$project_dir/$build_dir/sdkconfig"
    -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.supermini
  )
fi

idf.py "${cmake_args[@]}" build
if [[ "$board" == "supermini" ]]; then
  # Super Mini uses a 4 MB partition table; write bootloader + table + app.
  idf.py -B "$build_dir" -p "$port" flash
else
  idf.py -B "$build_dir" -p "$port" app-flash
fi
