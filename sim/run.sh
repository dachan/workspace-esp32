#!/bin/bash
# Builds and runs the desktop creature renderer, then opens the result.
# No ESP-IDF, no board, no flash cycle — same creature.c and gfx.c as the firmware.
set -e
cd "$(dirname "$0")/.."

cc -O2 -std=gnu17 -Imain \
   sim/sim.c main/creature.c main/gfx.c main/canvas.c \
   -lm -o sim/creature_sim

./sim/creature_sim

# BMP is trivial to write but awkward to view; convert for convenience.
if command -v sips >/dev/null 2>&1; then
    sips -s format png sim/out.bmp --out sim/out.png >/dev/null 2>&1
    rm -f sim/out.bmp
    echo "open sim/out.png"
    [ "$1" = "--open" ] && open sim/out.png
fi
