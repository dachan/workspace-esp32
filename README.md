# ESP32 workspace

Dedicated workspace for the paired ESP32-S3 radar displays and related LED
firmware.

- `radar/` — active 3.5-inch LD2450 sensor/transmitter and
  2.8-inch ESP-NOW receiver firmware, display renderer, voice control, touch,
  joystick, backups, and flash tooling.
- `hardware-test/` — shared non-radar peripheral drivers used by the radar
  application’s hardware-test harness.
- `rainbow-wave/` — standalone eight-pixel WS2812B firmware.
- `super-tamagotchi/` — portable ESP32-S3 virtual-pet firmware, including the
  native creature simulator and the display, touch, and audio drivers.
- `<hardware-reference-image>` — retained hardware reference image.

Read `AGENTS.md` before modifying or flashing either radar board; read the
nested project instructions before working on Super Tamagotchi.

The repository ignores generated ESP-IDF build directories and managed
dependencies. The two pre-radar-link board backups remain tracked source
artifacts under `radar/backups/`.
