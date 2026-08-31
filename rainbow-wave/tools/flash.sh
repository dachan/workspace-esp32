#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
idf_export="$IDF_PATH/export.sh"
port="${ESP_PORT:-/dev/ttyUSB0}"

source "$idf_export" >/dev/null
cd "$project_dir"
idf.py -p "$port" build flash
