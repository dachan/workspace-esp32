# Hardware-test drivers

This folder contains the non-radar peripheral drivers used by the radar
application’s hardware-test harness. Keeping them here separates display,
touch, input, audio, and LED support from the LD2450 and ESP-NOW transport
sources while preserving one firmware build for each board role.

The `radar/main/CMakeLists.txt` file includes these sources directly. This is a
source subproject, not a separately flashed firmware image.

## Current shared wiring

These allocations describe the peripherals supported by the shared drivers.
They are not one standalone harness: the active firmware selects the subset it
needs. `NC` means deliberately not connected.

### Display and touch header

The current ILI9341/FT6336 display profiles use this 14-pin module header.

Physical header layout:

```text
Pin:   1    2    3        4         5          6         7    8    9        10       11       12       13       14
Name: VCC  GND  LCD_CS   LCD_RST   LCD_RS/DC  SDI/MOSI  SCK  LED  SDO/MISO CTP_SCL  CTP_RST  CTP_SDA  CTP_INT  SD_CS
Wire: 3V3  GND  GPIO11   GPIO10    GPIO9      GPIO8     GPIO18 GPIO17 GPIO16  GPIO15   GPIO7    GPIO6    GPIO5    GPIO4
```

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

The button inputs use internal pull-ups. The on-board addressable RGB LED is
controlled separately on GPIO48; it has no external wiring. Speech recognition
adds no pins because it consumes the INMP441 driver’s audio stream.

## Sources

- Display profile and transfer synchronization
- Touch calibration
- Joystick and microphone input
- Speech recognition
- On-board RGB LED, colour palettes, and the WS2812B rainbow wave
