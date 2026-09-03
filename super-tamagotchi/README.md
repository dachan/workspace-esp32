# Super Tamagotchi

A portable, battery-powered virtual pet built on an ESP32-S3. It has no menus:
the touchscreen is direct interaction with the creature, and the display and
speaker are its primary outputs.

See [WIRING.md](WIRING.md) for the authoritative wiring map.

## Build and flash

The project targets `esp32s3` and uses the workspace ESP-IDF toolchain:

```bash
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
cd super-tamagotchi
idf.py set-target esp32s3
idf.py build flash monitor
```

`sdkconfig.defaults` carries board-specific settings. `sdkconfig`, build
directories, managed components, and simulator binaries are generated locally
and intentionally ignored.

## Desktop simulator

`creature.c` and `gfx.c` render against a platform-neutral RGB565 canvas, so
the creature can be inspected without hardware:

```bash
./sim/run.sh --live  # interactive display at http://localhost:8765
./sim/run.sh         # renders stills to sim/out.png
```

Do not commit device credentials or other local configuration. Generated
`sdkconfig`, build directories, simulator output, and environment files are
ignored.
