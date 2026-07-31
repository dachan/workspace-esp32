#!/bin/bash
# Desktop builds of the creature. Same creature.c and gfx.c as the firmware —
# no ESP-IDF, no board, no flash cycle.
#
#   ./sim/run.sh          build both, render stills
#   ./sim/run.sh --live   build both, then serve the interactive emulator
#
# Note the simulator cannot catch embedded resource limits: a desktop process has
# megabytes of stack, so code that reboots the board on a stack overflow will run
# here quite happily. Use it for artwork and motion, not for proving the firmware.
set -e
cd "$(dirname "$0")/.."

CFLAGS="-O2 -std=gnu17 -Imain -Wall"
SHARED="main/creature.c main/gfx.c main/canvas.c"

cc $CFLAGS sim/sim.c  $SHARED -lm -o sim/creature_sim
cc $CFLAGS sim/live.c $SHARED -lm -o sim/creature_live

if [ "$1" = "--live" ]; then
    exec python3 sim/serve.py
fi

./sim/creature_sim >/dev/null

if command -v sips >/dev/null 2>&1; then
    sips -s format png sim/out.bmp --out sim/out.png >/dev/null 2>&1
    echo "stills:      sim/out.png"
fi
rm -f sim/out.bmp sim/film.bmp

echo "interactive: ./sim/run.sh --live"
