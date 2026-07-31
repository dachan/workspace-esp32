#!/bin/bash
# Builds and runs the desktop creature renderer.
# No ESP-IDF, no board, no flash cycle — same creature.c and gfx.c as the firmware.
#
#   ./sim/run.sh          render stills + animation
#   ./sim/run.sh --open   ...and open the animation in a browser
set -e
cd "$(dirname "$0")/.."

cc -O2 -std=gnu17 -Imain -Wall \
   sim/sim.c main/creature.c main/gfx.c main/canvas.c \
   -lm -o sim/creature_sim

GEOM=$(./sim/creature_sim)

# Stills, for looking at shapes frame by frame.
if command -v sips >/dev/null 2>&1; then
    sips -s format png sim/out.bmp --out sim/out.png >/dev/null 2>&1
fi
rm -f sim/out.bmp

# Animation, for judging motion — which is most of what matters here.
python3 sim/make_page.py $GEOM
rm -f sim/film.bmp

echo "open sim/creature.html"
[ "$1" = "--open" ] && open sim/creature.html
exit 0
