# Dual-display date/time wiring

Firmware build label:

`ESP32_S3-13_tft_240x240+096_oled_128x64-Date_Time`

The new board is an ESP32-S3. The pictured TFT is the 7-pin SPI ST7789 module
marked `BLK, DC, RES, SDA, SCK, VCC, GND`; it is not the CS-equipped pinout
shown in the earlier diagram. The OLED is the 4-pin I2C SSD1306 module. Both
displays use the 3.3 V rail and a common ground. Disconnect USB power before
changing wiring.

## Direct wiring

### 1.3-inch ST7789 TFT, 7-pin header (pictured module)

| TFT input | ESP32-S3 pin |
|---|---|
| BLK | 3V3 |
| DC | GPIO10 |
| RES / RST | GPIO8 |
| SDA / MOSI | GPIO11 |
| SCK / CLK | GPIO12 |
| VCC | 3V3 |
| GND | GND |

There is no exposed `CS` pin on this module; its chip select is hard-wired on
the board, and the firmware leaves the ESP32 CS output disabled. `BLK` is the
backlight supply and should be connected to 3V3 for always-on brightness. On
this display, `SDA` means SPI serial data/MOSI and `SCK` means SPI clock; neither
is the OLED's I2C signal. Because CS is hard-wired on this variant, the firmware
uses SPI mode 3.

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

### Joystick switch module, 8-pin header

The pictured module is a passive switch bank. Connect its `COM` pin to ground;
do not connect a module VCC pin. The firmware enables ESP32 internal pull-ups,
so every switch input is active-low when pressed.

| Joystick pin | ESP32-S3 pin |
|---|---|
| COM | GND |
| UP | GPIO13 |
| DOWN | GPIO14 |
| LEFT | GPIO15 |
| RIGHT | GPIO16 |
| MID | GPIO17 |
| SEL | GPIO18 |
| RST | GPIO4 |

Controls while the normal clock is shown:

- `SEL` enters date/time edit mode; press it again to advance through year,
  month, day, hour, minute, and second.
- `LEFT`/`RIGHT` select the previous/next field, and `UP`/`DOWN` change it.
- `MID` saves the edited time to NVS; `RST` cancels the edit. `RST` is a
  firmware cancel input, not the ESP32's hardware reset pin.

## Connector-order view

```text
ST7789 7P            ESP32-S3             SSD1306 4P
BLK  --------------- 3V3
DC   --------------- GPIO10
RES  --------------- GPIO8
SDA  --------------- GPIO11 (SPI MOSI)
SCK  --------------- GPIO12 (SPI CLK)
VCC  --------------- 3V3  --------------- VCC
GND  --------------- GND  --------------- GND
                                              SCL -------- GPIO7
                                              SDA -------- GPIO6

Joystick 8P          ESP32-S3
COM  --------------- GND
UP   --------------- GPIO13
DOWN --------------- GPIO14
LEFT --------------- GPIO15
RIGHT -------------- GPIO16
MID  --------------- GPIO17
SEL  --------------- GPIO18
RST  --------------- GPIO4
```

Keep GPIO0/3/45/46 and the native USB pins out of this display wiring. If the
TFT remains blank while the OLED works, check the panel controller marking:
The supplied module is marked `Driver IC: ST7789`. If the controller marking
on a replacement differs, verify its initialization sequence before reusing
this build.

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
serial console, or use the joystick controls above. The value is saved in NVS
and the OLED shows the centered `HH:MM` time above the centered date, updating once per
second. Both text lines follow an eight-position ±2-pixel shift pattern that advances
every 30 minutes to spread OLED wear. The TFT independently shows a never-ending
fluid-motion screensaver
with a 32-step light-pastel gradient from each hardware-random HSV base tint
toward a soft highlight with the restored 64/255 lift. Each next target is at least 115
RGB-distance units from the previous base colour. Palette progression updates
every 20 frames and fluid phase advances at 2/5 phase units per frame, making
each visual cycle 25% longer than the previous settings, with a stronger Gaussian
blur. The
firmware starts from its compile timestamp until a clock value is received.
