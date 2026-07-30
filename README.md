# super-tamagotchi

A portable, battery-powered virtual pet built on an ESP32-S3 — 2.8" colour TFT with touch, 6-axis
IMU, and I2S audio in and out. Handled and spoken to like a living thing, with no menus.

Hardware inventory, wiring constraints, and project conventions live in [AGENTS.md](AGENTS.md).

## Status

**Step 0 bring-up only.** Nothing is wired yet. `main/main.c` validates the board and toolchain
before any peripheral goes on: console, 16MB flash, and that the 8MB octal PSRAM both enumerates and
correctly stores data.

## Build

Requires ESP-IDF v5.5 with the `esp32s3` target.

```bash
. $HOME/esp/esp-idf/export.sh && idf.py set-target esp32s3 && idf.py build flash monitor
```

Expected output ends with `step 0 PASSED — safe to wire the display`.

Board-specific settings live in `sdkconfig.defaults`; the generated `sdkconfig` is gitignored, so
edit the defaults and rebuild rather than the generated file.

## Configuration

WiFi credentials and any other secrets belong in NVS on the device or a local gitignored file —
never in committed firmware sources. See [AGENTS.md](AGENTS.md).
