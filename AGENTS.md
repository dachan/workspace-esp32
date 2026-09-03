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

## Super Tamagotchi

This portable ESP32-S3 virtual pet combines a display, capacitive touch, and
speaker/amplifier audio. The desktop simulator renders the same creature code
without hardware. Use [super-tamagotchi/WIRING.md](super-tamagotchi/WIRING.md)
as the authoritative pin map, keep logic signals at 3.3 V, and disconnect power
before changing wiring.

Build and flash it with a local ESP-IDF installation:

```sh
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
cd super-tamagotchi
idf.py set-target esp32s3
idf.py build flash monitor
```

Choose the serial device through ESP-IDF options or an explicit local
environment variable. Do not hard-code machine-specific ports or paths. The
platform-neutral simulator can be run with `./sim/run.sh` or `./sim/run.sh
--live`; live mode serves frames on localhost only.

Keep rendering, hardware drivers, and application state in focused modules;
prefer small functions and straightforward control flow; and document
non-obvious hardware constraints in comments or wiring docs. Generated
binaries, simulator output, `sdkconfig`, environment files, credentials, audio
recordings, device dumps, and other private data must not be committed.

## Wiring documentation

When documenting a user-supplied module diagram, preserve its physical header
numbering and order. Use a table for direct peripheral-to-board wiring, pair
each signal with its ESP32 power rail or GPIO, and mark deliberately unused
connections as `NC`. Default to a monospaced text diagram unless an image is
explicitly requested. Do not silently substitute a different board pinout.

For a two-row peripheral header, use five columns in physical row order with a
blank separator column:

`peripheral input | ESP32 pin |  | ESP32 pin | peripheral input`

Order each side from the peripheral outward: list connector inputs in their
physical or functional order, followed by the matched ESP32 GPIO or power pin.
Prefer consecutive ESP32 GPIOs that follow the peripheral's pin order when the
board and fixed firmware requirements allow it. Keep one peripheral together
on the first available side; do not split it across the separator until that
side is full.

For a direct peripheral-to-board map, use one continuous
`peripheral | GPIO/power` pair in connector order. Do not place a second
peripheral sequence alongside it; list unused GPIO coverage in additional rows
or a separate audit table. Include every GPIO exposed on the pictured board,
marking unused connections as `NC`.
