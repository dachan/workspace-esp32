# ESP32 workspace

ESP32-S3 firmware for radar displays, shared peripherals, addressable LEDs, and
a portable virtual pet.

## Projects

- `radar/` — 3.5-inch LD2450 sensor/transmitter and 2.8-inch ESP-NOW receiver
  firmware, display renderer, voice control, touch, joystick, and flash tooling.
- `hardware-test/` — shared non-radar peripheral drivers.
- `rainbow-wave/` — standalone eight-pixel WS2812B firmware.
- `super-tamagotchi/` — portable ESP32-S3 virtual-pet firmware, including the
  native creature simulator and the display, touch, and audio drivers.

## Wiring

Project wiring diagrams use `NC` for deliberately unused connections.

```text
LD2450 --UART--> radar transmitter --ESP-NOW--> radar receiver --SPI--> display
                    |
                    +--GPIO1--> WS2812B motion bar

rainbow-wave firmware --GPIO1--> WS2812B bar

Super Tamagotchi firmware --SPI/I2C--> display and touch
                           --I2S-----> amplifier --> speaker
```

| Project | Wiring documentation |
|---|---|
| Radar pair | [radar/README.md](radar/README.md) |
| Shared drivers | [hardware-test/README.md](hardware-test/README.md) |
| Standalone LED demo | [rainbow-wave/README.md](rainbow-wave/README.md) |
| Super Tamagotchi | [README](super-tamagotchi/README.md) · [WIRING](super-tamagotchi/WIRING.md) |

Set `IDF_PATH` to a local ESP-IDF installation before using the flash scripts.

Generated ESP-IDF build directories, managed dependencies, local configuration,
simulator output, and firmware images are intentionally ignored.
