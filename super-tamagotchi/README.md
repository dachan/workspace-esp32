# Super Tamagotchi

A portable ESP32-S3 virtual pet with touchscreen interaction, display graphics,
and speaker output.

See [WIRING.md](WIRING.md) for the authoritative wiring map.

## Wiring

Physical connection view:

```text
ESP32-S3 --SPI + control--> ILI9341V display
         --I2C------------> FT6336G capacitive touch
         --I2S------------> MAX98357 amplifier --speaker output--> speaker
         --reserved-------> LSM6DS3 IMU (not connected)
```

The display uses `3V3`; the amplifier `VIN` uses `5V`. See [WIRING.md](WIRING.md)
for the complete pin map.

## Build and flash

Build for `esp32s3` with ESP-IDF:

```bash
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
cd super-tamagotchi
idf.py set-target esp32s3
idf.py build flash monitor
```

## Desktop simulator

The simulator renders the creature without hardware:

```bash
./sim/run.sh --live  # interactive display at http://localhost:8765
./sim/run.sh         # renders stills to sim/out.png
```
