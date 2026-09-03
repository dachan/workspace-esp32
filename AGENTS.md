# ESP32 workspace instructions

## Project scope

This repository contains ESP32-S3 firmware for an LD2450 radar display pair,
shared peripheral drivers, a WS2812B demo, and a portable virtual pet. The
radar application is in `radar/`; shared non-radar drivers are in
`hardware-test/`; the standalone LED demo is in `rainbow-wave/`; and the
virtual pet is in `super-tamagotchi/`.

Read the relevant project README and `super-tamagotchi/WIRING.md` before
changing firmware or connecting hardware.

## Safety and public-repository hygiene

- Verify the intended board and serial device before flashing.
- Prefer `app-flash` for ordinary radar firmware updates when supported by the
  installed tooling.
- Do not commit credentials, device NVS dumps, full-flash images, generated
  configuration, local paths, or hardware-specific identifiers.
- Keep Wi-Fi credentials and other secrets in ignored local configuration or
  device storage.
- Do not write or run tests unless the user explicitly requests it. Firmware
  builds and live serial checks are allowed when needed for the requested work.

## Radar application

The transmitter reads LD2450 target frames over UART, broadcasts them with
ESP-NOW, and drives an external WS2812B motion indicator. The receiver renders
the radar view and does not require the transmitter's joystick, microphone, or
voice controls. The receiver and transmitter should remain visually and
behaviorally equivalent where their different panel sizes allow.

The LD2450 uses 5 V power, 3.3 V UART logic, and a UART connection with the
sensor TX line connected to the ESP32 RX GPIO and the sensor RX line connected
to the ESP32 TX GPIO. The driver probes supported baud rates and treats a valid
target frame as healthy radar data even if a configuration command times out.

The radar UI uses a black-and-green palette, a proportional -60 to +60 degree
fan, a visual-left distance readout, an edge-aligned coordinate readout, and a
fading detected-person marker. After an inactivity timeout the display sleeps
while the ESP32 continues listening; a new detection or supported touch action
redraws it.

## Build and flash

Use a local ESP-IDF installation without embedding its path in scripts or
documentation:

```sh
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
cd radar

# Transmitter
idf.py -B build-radar-transmitter \
  -D RADAR_LINK_ROLE=transmitter build
ESP_PORT=/dev/ttyUSB0 ./tools/flash-radar.sh transmitter

# Receiver
idf.py -B build-radar-receiver-accel \
  -D RADAR_LINK_ROLE=receiver build
ESP_PORT=/dev/ttyUSB1 ./tools/flash-radar.sh receiver
```

The flash scripts require both `IDF_PATH` (or `IDF_EXPORT`) and `ESP_PORT` so
that no machine-specific installation path or device identity is stored in the
repository.

## Wiring documentation

When documenting a user-supplied module diagram, preserve its physical header
numbering and order. Use a table for direct peripheral-to-board wiring, pair
each signal with its ESP32 power rail or GPIO, and mark deliberately unused
connections as `NC`. Do not silently substitute a different board pinout.
