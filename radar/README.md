# ESP32-S3 radar display firmware

Saved, re-flashable firmware for a paired ESP32-S3 radar-display setup. The
3.5-inch sensor/transmitter reads the LD2450 and broadcasts targets over
ESP-NOW; the 2.8-inch receiver mirrors the radar view without a reticle.

This is one role-configurable app: the same source is compiled as either the
sensor/transmitter or display/receiver using `RADAR_LINK_ROLE`. The shared
non-radar peripheral drivers live in `../hardware-test/main/` and are compiled
into this app; the two board profiles are kept in separate build directories.

## Quick flash

With the ESP32 connected by USB:

```sh
./tools/flash-radar.sh sensor-3.5
./tools/flash-radar.sh receiver-2.8
```

The active names are:

- `sensor-3.5` — active ST7796U sensor/transmitter display
- `receiver-2.8` — active ILI9341 ESP-NOW receiver display
- `original-2.8` and `current-3.2` — retained local/legacy profiles

Set `ESP_PORT` first if the USB device name changes:

```sh
ESP_PORT=/dev/ttyUSB0 ./tools/flash-radar.sh current-3.2
```

Each profile builds in its own directory, so switching displays never reuses a
firmware binary compiled for the other resolution.

## Retained 3.2-inch local profile

```text
CONTROLLER: ILI9341V                 TOUCH: FT6336U at I2C address 0x38
RESOLUTION: 240 x 320                SPI: 40 MHz

DISPLAY PIN                           ESP32-S3 DEVKITC-1
VCC                                   3V3 for the original display
GND                                   GND
SD_CS                                 GPIO4
CTP_INT                               GPIO5
CTP_SDA                               GPIO6
CTP_RST                               GPIO7
CTP_SCL                               GPIO15
SDO/MISO                              GPIO16
LED                                   GPIO17
SCK                                   GPIO18
SDI/MOSI                              GPIO8
LCD_RS/DC                             GPIO9
LCD_RST                               GPIO10
LCD_CS                                GPIO11
```

The replacement listing describes a
5V supply with 3.3V-compatible logic; if its own board marking or documentation
specifies `5V`, use the ESP32 `5V` pin for **VCC only**. All data, touch, and
ground pins remain exactly as shown above.

## Retained 2.8-inch local profile

```text
CONTROLLER: ILI9341                  TOUCH: FT6336U at I2C address 0x38
RESOLUTION: 240 x 320                SPI: 40 MHz
FLASH NAME: original-2.8             SEPARATE TOUCH CALIBRATION: yes

DISPLAY PIN                           ESP32-S3 DEVKITC-1
VCC                                   3V3
GND                                   GND
SD_CS                                 GPIO4
CTP_INT                               GPIO5
CTP_SDA                               GPIO6
CTP_RST                               GPIO7
CTP_SCL                               GPIO15
SDO/MISO                              GPIO16
LED                                   GPIO17
SCK                                   GPIO18
SDI/MOSI                              GPIO8
LCD_RS/DC                             GPIO9
LCD_RST                               GPIO10
LCD_CS                                GPIO11
```

`original-2.8` uses the same FT6336U capacitive-touch wiring as the 3.2-inch
profile, with a landscape 320x240 view and its own calibration slot.

### External 8-pixel rainbow bar

The 2.8-inch build also drives the external WS2812B rainbow bar. This does
not change any display wiring.

```text
WS2812B-8 INPUT    ESP32-S3 DEVKITC-1
VCC                3V3
GND                GND
DIN                RIGHT HEADER GPIO1
```

### Palette buttons

Use momentary, normally-open pushbuttons. The firmware holds GPIO2 and GPIO21
high internally. No external resistors are needed.

```text
BUTTON                 ESP32-S3 DEVKITC-1
NEXT ONE SIDE          RIGHT HEADER GPIO2
NEXT OTHER SIDE        GND
PREVIOUS ONE SIDE      RIGHT HEADER GPIO21
PREVIOUS OTHER SIDE    GND
```

The palette order is Northern Lights, Deep Ocean, Desert Sunset, Tropical
Lagoon, Forest Canopy, Molten Ember, Lavender Dawn, and Neon Arcade. GPIO2
moves forward and GPIO21 moves backward. Both change the LED bar only; the
radar view stays black and green.

### LD2450 person tracking radar

The radar view keeps the sensor at the top centre of the display and maps the
LD2450's lateral `x` and forward `y` coordinates onto the screen. Its half-metre
range rings cover 0-6 m and its radial boundaries match the sensor's -60 to +60
degree field of view. The fan is centred vertically between the distance and
coordinate text. The active setup uses single-person tracking and lists the
target's X/Y coordinates in metres.

Disconnect USB power before wiring. Follow the signal labels on the modules:

```text
LD2450 SIGNAL       ESP32-S3 DEVKITC-1
5V                  5V
GND                 GND
TX                  RIGHT HEADER GPIO41 (ESP32 receive)
RX                  RIGHT HEADER GPIO42 (ESP32 transmit)
```

The driver probes every supported LD2450 UART rate. The current module reports
valid target frames at 9600 baud after its `2.14.25112412` firmware update. Its
5V supply must support more than 200 mA; its UART logic is 3.3V.

## 3.5-inch active sensor/transmitter profile

```text
CONTROLLER: ST7796U                  TOUCH: FT6336U at I2C address 0x38
RESOLUTION: 320 x 480                SPI: 40 MHz

DISPLAY SIGNAL                        ESP32-S3 DEVKITC-1
SD_CS                                 GPIO4
CTP_INT                               GPIO5
CTP_SDA                               GPIO6
CTP_RST                               GPIO7
CTP_SCL                               GPIO15
SDO/MISO                              GPIO16
LED                                   GPIO17
SCK                                   GPIO18
SDI/MOSI                              GPIO8
LCD_RS/DC                             GPIO9
LCD_RST                               GPIO10
LCD_CS                                GPIO11
GND                                   GND
```

Verify `VCC` against the display board's markings before changing its wiring.

## Behaviour

- The backlight pulses off, then turns on.
- The firmware sends six colour bands, then starts the radar view immediately.
- A three-second touch hold starts the five-point calibration:
  top-left, top-right, bottom-right, bottom-left, centre.
- A three-second hold on the calibration screen shows SLEEP and POWER OFF.
  Sleep is deep sleep with tap-to-wake. Power off is deep sleep with no wake
  source; use EN/RESET or cycle power to start again.
- Calibration is stored separately for each resolution, so switching profiles
  never applies 3.2-inch touch coordinates to the 3.5-inch display.
- The test only reads microSD identity/capacity; it never writes to the card.
- GPIO2 advances through eight palettes; GPIO21 returns to the previous palette.
  Palettes affect the LED bar only.
- The radar is black and green, with its sensor at the physical top centre,
  distance at visual top-left, target coordinates near the bottom-left, and
  proportional margins on both display sizes. After the landscape MADCTL,
  framebuffer +X is visual left; label origins come from `radar_text_left_x()`.
  Do not place strings at framebuffer x=8. The locked mapping is in AGENTS.md.
- An active target is shown as an 8x8 green dot. Every two seconds it expands
  to 48 px while fading to transparent in 0.25 seconds.
- After 10 seconds with no detected person the display sleeps (panel and
  backlight off). The next detected person or a tap wakes it and redraws the
  view. The ESP32 stays running so the sensor UART or ESP-NOW link can still
  arrive.

## Voice commands

Say `up`, `down`, `left`, `right`, `up right`, `up left`, `down right`, or
`down left` to move the reticle. Say `reset` to return the reticle to the exact
centre of the display. There is no wake word. Receiver-role builds do not draw
the reticle; transmitter and local builds retain it.
