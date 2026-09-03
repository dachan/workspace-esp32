# Hardware-test drivers

Shared non-radar peripheral drivers for display, touch, input, audio, and LED
support.

The radar build includes these sources directly. This directory is not a
separately flashed firmware project.

## Shared wiring

These allocations cover the peripherals supported by the shared drivers. The
active firmware selects the subset it needs. `NC` means deliberately unused.

### Display and touch header

The current ILI9341/FT6336 display profiles use this 14-pin module header.

Physical header layout, pins 1 through 14:

| Pin | Module signal | ESP32 connection |
|---:|---|---|
| 1 | `VCC` | `3V3` |
| 2 | `GND` | `GND` |
| 3 | `LCD_CS` | GPIO11 |
| 4 | `LCD_RST` | GPIO10 |
| 5 | `LCD_RS/DC` | GPIO9 |
| 6 | `SDI/MOSI` | GPIO8 |
| 7 | `SCK` | GPIO18 |
| 8 | `LED` | GPIO17 |
| 9 | `SDO/MISO` | GPIO16 |
| 10 | `CTP_SCL` | GPIO15 |
| 11 | `CTP_RST` | GPIO7 |
| 12 | `CTP_SDA` | GPIO6 |
| 13 | `CTP_INT` | GPIO5 |
| 14 | `SD_CS` | GPIO4 |

Peripheral-order map:

```text
VCC       -> ESP32 3V3
GND       -> ESP32 GND
LCD_CS    -> ESP32 GPIO11
LCD_RST   -> ESP32 GPIO10
LCD_RS/DC -> ESP32 GPIO9
SDI/MOSI  -> ESP32 GPIO8
SCK       -> ESP32 GPIO18
LED       -> ESP32 GPIO17
SDO/MISO  -> ESP32 GPIO16
CTP_SCL   -> ESP32 GPIO15
CTP_RST   -> ESP32 GPIO7
CTP_SDA   -> ESP32 GPIO6
CTP_INT   -> ESP32 GPIO5
SD_CS     -> ESP32 GPIO4 (held inactive when the slot is unused)
```

### Joystick

```text
Joystick connector order: GND, +V, VRx, VRy, SW
GND -> ESP32 GND
+V  -> ESP32 3V3
VRx -> ESP32 GPIO12 (ADC)
VRy -> ESP32 GPIO13 (ADC)
SW  -> ESP32 GPIO14 (active-low input)
```

### INMP441 microphone

```text
INMP441 connector order: SCK, WS, SD, L/R, VDD, GND
SCK -> ESP32 GPIO38 (I2S BCLK)
WS  -> ESP32 GPIO39 (I2S word select)
SD  -> ESP32 GPIO40 (I2S data)
L/R -> ESP32 GND (left channel)
VDD -> ESP32 3V3
GND -> ESP32 GND
```

### External rainbow bar and palette buttons

```text
WS2812B connector order: VCC, DIN, GND, DOUT
VCC  -> ESP32 3V3
DIN  -> ESP32 GPIO1
GND  -> ESP32 GND
DOUT -> NC (single bar)

Next palette button:     GPIO2  <-> GND
Previous palette button: GPIO21 <-> GND
```

The button inputs use internal pull-ups. The on-board addressable RGB LED uses
GPIO48. Speech recognition reuses the microphone’s audio stream.

## Drivers

- Display profile and transfer synchronization
- Touch calibration
- Joystick and microphone input
- Speech recognition
- On-board RGB LED, colour palettes, and the WS2812B rainbow wave
