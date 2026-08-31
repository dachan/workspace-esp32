# Super Tamagotchi

A portable, battery-powered virtual pet built on an ESP32-S3. It has no menus:
the touchscreen is direct interaction with the creature, and the display and
speaker are its primary outputs.

## Current status

The 2.8-inch ILI9341V display, FT6336G-family capacitive touch controller, and
MAX98357 speaker output are wired and were verified on hardware on 2026-08-31.
The creature renders at about 31.5 fps and touch contacts produce screen
coordinates. The IMU, microphone input, and battery-power system are still to
be brought up.

Hardware decisions, current pin assignments, and flashing conventions are in
[AGENTS.md](AGENTS.md); use [WIRING.md](WIRING.md) as the authoritative wiring
map.

## Build and flash

The project targets `esp32s3` and uses the workspace ESP-IDF toolchain:

```bash
source $IDF_PATH/export.sh
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

## Configuration

WiFi credentials and other secrets belong in NVS on the device or a local
gitignored file—never in committed firmware sources.
