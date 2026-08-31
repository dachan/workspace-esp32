# ESP32-S3 rainbow wave

Firmware for the pictured eight-pixel WS2812B bar. It renders a continuously
moving rainbow/brightness wave from the physical left side to the right side.

## Wiring used

```text
WS2812B-8 input     ESP32-S3 DevKitC-1
VCC                 3V3
GND                 GND
DIN                 GPIO1
```

The bar is wired from its right-hand input end, so the firmware reverses the
logical pixel order to keep the visible motion left-to-right. Brightness is
limited for USB/3.3V operation.

## Flash

```sh
./tools/flash.sh
```

If the USB serial device changes:

```sh
ESP_PORT=/dev/ttyUSB0 ./tools/flash.sh
```
