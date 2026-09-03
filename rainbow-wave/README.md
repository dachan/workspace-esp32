# ESP32-S3 rainbow wave

Firmware for the pictured eight-pixel WS2812B bar. It renders a continuously
moving rainbow/brightness wave from the physical left side to the right side.

## Current wiring

Physical connection view. The data input is at the bar’s physical right-hand
end; the firmware reverses logical pixel order so the visible wave travels
left-to-right.

```text
WS2812B-8 bar                       ESP32-S3
-----------                         ---------
VCC       <------------------------ 3V3
DIN       <------------------------ GPIO1
GND       <------------------------ GND
DOUT      ------------------------> NC
```

Peripheral-order map:

```text
WS2812B connector order: VCC, DIN, GND, DOUT
VCC  -> ESP32 3V3
DIN  -> ESP32 GPIO1
GND  -> ESP32 GND
DOUT -> NC (single bar)
```

Brightness is limited for USB/3.3V operation.

## Flash

```sh
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
ESP_PORT=/dev/ttyUSB0 ./tools/flash.sh
```

Set `ESP_PORT` to the serial device for the connected board:

```sh
ESP_PORT=/dev/ttyUSB0 ./tools/flash.sh
```
