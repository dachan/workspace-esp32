# Hardware-test drivers

This folder contains the non-radar peripheral drivers used by the radar
application’s hardware-test harness. Keeping them here separates display,
touch, input, audio, and LED support from the LD2450 and ESP-NOW transport
sources while preserving one firmware build for each board role.

The `radar/main/CMakeLists.txt` file includes these sources directly. This is a
source subproject, not a separately flashed firmware image.

## Sources

- Display profile and transfer synchronization
- Touch calibration
- Joystick and microphone input
- Speech recognition
- On-board RGB LED, colour palettes, and the WS2812B rainbow wave
