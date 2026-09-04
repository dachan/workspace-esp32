# ESP32-S3 LD2450 radar

The transmitter reads LD2450 target frames over UART, broadcasts them over
ESP-NOW channel 6, and drives an external WS2812B motion bar. The receiver
renders the radar display from those frames.

## Current wiring

### Transmitter: LD2450 and motion bar

Physical connection view:

```text
LD2450                    ESP32-S3 transmitter       WS2812B-8 bar
------                    -------------------       ------------
5V       ----------------> 5V
GND      ----------------> GND
TX       ----------------> GPIO41 (UART RX)
RX       <---------------- GPIO42 (UART TX)

                                                      VCC <------ 3V3
GPIO1    -------------------------------------------> DIN
                                                      GND <------ GND
                                                      DOUT ------ NC
```

Peripheral-order maps:

```text
LD2450 connector order: 5V, GND, TX, RX
5V       -> ESP32 5V
GND      -> ESP32 GND
TX       -> ESP32 GPIO41 (UART RX)
RX       -> ESP32 GPIO42 (UART TX)

WS2812B connector order: VCC, DIN, GND, DOUT
VCC      -> ESP32 3V3
DIN      -> ESP32 GPIO1
GND      -> ESP32 GND
DOUT     -> NC (single bar)
```

### Receiver: 2.8-inch display module

The receiver uses the LCD SPI/control signals and the FT6336 capacitive touch
controller. The microSD chip-select line is not used by this firmware.

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
| 14 | `SD_CS` | `NC` |

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
SD_CS     -> NC
```

When awake, hold the receiver screen for three seconds to open calibration,
then lift your finger and tap SLEEP or POWER OFF. The display blanks after ten
seconds without an active target but the receiver keeps listening. A new target
or a tap wakes it. Both SLEEP and POWER OFF wake from a tap on the receiver;
POWER OFF also holds the LCD reset low while the board remains in deep sleep.

The LD2450 UART logic is 3.3 V. Its 5 V supply must support more than 200 mA.
The installed module produces valid target frames at 9600 baud; the firmware
probes its supported UART rates at startup.

## Build and flash

```sh
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
cd radar
idf.py -B build-radar-transmitter -D RADAR_LINK_ROLE=transmitter build
ESP_PORT=/dev/ttyUSB0 ./tools/flash-radar.sh transmitter

# Receiver/display board
idf.py -B build-radar-receiver -D RADAR_LINK_ROLE=receiver build
ESP_PORT=/dev/ttyUSB1 ./tools/flash-radar.sh receiver
```

Use `app-flash` for normal updates. It preserves the existing partition table
and any non-application data.
