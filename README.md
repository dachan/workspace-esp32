# ESP32 workspace

Dedicated workspace for the paired ESP32-S3 radar displays and related LED
firmware.

- `radar/` — 3.5-inch LD2450 sensor/transmitter and 2.8-inch ESP-NOW receiver
  firmware, display renderer, voice control, touch, joystick, and flash tooling.
- `hardware-test/` — shared non-radar peripheral drivers used by the radar
  application’s hardware-test harness.
- `rainbow-wave/` — standalone eight-pixel WS2812B firmware.
- `super-tamagotchi/` — portable ESP32-S3 virtual-pet firmware, including the
  native creature simulator and the display, touch, and audio drivers.
Each project README documents its wiring, build, and flashing workflow. Set
`IDF_PATH` to a local ESP-IDF installation before using the flash scripts.

Generated ESP-IDF build directories, managed dependencies, local configuration,
simulator output, and firmware images are intentionally ignored.
