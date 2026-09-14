# Dual-display date/time wiring

Firmware build label:

`ESP32_S3-13_tft_240x240+096_oled_128x64-Date_Time`

The new board is an ESP32-S3. The TFT is the 7-pin SPI module and the OLED is
the 4-pin I2C SSD1306 module. Both displays use the 3.3 V rail and a common
ground. Disconnect USB power before changing wiring.

## Direct wiring

### 1.3-inch ST7735 TFT, 7-pin header

| TFT input | ESP32-S3 pin |
|---|---|
| RST / RES | GPIO8 |
| CS | GPIO9 |
| DC / A0 | GPIO10 |
| SDA / MOSI | GPIO11 |
| SCL / CLK | GPIO12 |
| GND | GND |
| VCC | 3V3 |

On this SPI module, `SDA` means serial data/MOSI; it is not the OLED's I2C
SDA signal. The module's backlight is normally tied to VCC on this 7-pin
variant, so there is no separate firmware-controlled BL wire. If the specific
board exposes an additional LED/BL pad, leave it NC unless its documentation
requires a current-limited connection.

### 0.96-inch SSD1306 OLED, 4-pin header

| OLED input | ESP32-S3 pin |
|---|---|
| GND | GND |
| VCC | 3V3 |
| SCL | GPIO7 |
| SDA | GPIO6 |

The firmware uses the usual SSD1306 address `0x3C`. If the module has no
on-board I2C pull-ups, add approximately 4.7 kOhm from SDA to 3V3 and from SCL
to 3V3. Do not connect the TFT's SPI `SDA` to the OLED's I2C `SDA`; they are
different buses despite the shared label.

## Connector-order view

```text
ST7735 7P             ESP32-S3             SSD1306 4P
RST  ---------------- GPIO8
CS   ---------------- GPIO9
DC   ---------------- GPIO10
SDA  ---------------- GPIO11 (SPI MOSI)
SCL  ---------------- GPIO12 (SPI CLK)
GND  ---------------- GND  --------------- GND
VCC  ---------------- 3V3  --------------- VCC
                                              SCL -------- GPIO7
                                              SDA -------- GPIO6
```

Keep GPIO0/3/45/46 and the native USB pins out of this display wiring. If the
TFT remains blank while the OLED works, check the panel controller marking:
some square 240x240 modules sold as “ST7735” use an ST7789-compatible
controller and need a different initialization sequence.

## Build and set the clock

```sh
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
cd date-time-display
idf.py -B ESP32_S3-13_tft_240x240+096_oled_128x64-Date_Time \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32s3
idf.py -B ESP32_S3-13_tft_240x240+096_oled_128x64-Date_Time \
  -p "$ESP_PORT" build flash monitor
```

After flashing, send `TIME YYYY-MM-DD HH:MM:SS` over the 115200-baud USB
serial console. The value is saved in NVS and both displays update once per
second. The firmware starts from its compile timestamp until a clock value is
received.
