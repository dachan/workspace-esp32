# ESP32-S3 LD2450 radar

The sensor board is a headless transmitter: it reads LD2450 target frames over
UART, broadcasts them via ESP-NOW on Wi-Fi channel 6, and drives the external
WS2812B bar as a motion indicator.

The separate receiver board renders the radar display from frames received over
ESP-NOW. Its distance readout is followed by radial acceleration; all receiver
text is scaled to 80% of the former size.

## Current wiring

### Transmitter: LD2450 and motion bar

The transmitter is headless. Its only physical peripherals are the LD2450 and
the external eight-pixel WS2812B bar; the receiver link is wireless ESP-NOW on
channel 6.

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

The receiver uses the LCD SPI and control signals only. The touch controller
and microSD chip-select lines on the same module are not connected by this
firmware.

Physical header layout, in the module’s connector order:

```text
Pin:   1    2    3        4         5          6         7    8    9        10       11       12       13       14
Name: VCC  GND  LCD_CS   LCD_RST   LCD_RS/DC  SDI/MOSI  SCK  LED  SDO/MISO CTP_SCL  CTP_RST  CTP_SDA  CTP_INT  SD_CS
Wire: 3V3  GND  GPIO11   GPIO10    GPIO9      GPIO8     GPIO18 GPIO17 GPIO16  NC       NC       NC       NC       NC
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
CTP_SCL   -> NC
CTP_RST   -> NC
CTP_SDA   -> NC
CTP_INT   -> NC
SD_CS     -> NC
```

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
idf.py -B build-radar-receiver-accel -D RADAR_LINK_ROLE=receiver build
ESP_PORT=/dev/ttyUSB1 ./tools/flash-radar.sh receiver
```

Use `app-flash` for normal updates. It preserves the existing partition table
and any non-application data.

At boot, the firmware initializes the LD2450 and ESP-NOW transmitter, requests
single-person tracking, and then logs target data approximately once per second.
A single-target command timeout does not stop tracking: valid 30-byte target
frames continue to be broadcast.

The transmitter calculates the nearest tracked person's radial acceleration and
sends that one value in every ESP-NOW frame. The receiver's `ACCEL` readout and
the eight-pixel WS2812B bar both use that exact value. The bar is a moving green
light crest: it is very dim and slow at zero acceleration, then becomes brighter
and faster as acceleration increases. Its idle output uses three one-step frames
followed by one black frame, averaging 0.75 WS2812B brightness steps—another 50%
dimmer than the preceding idle setting.

On the receiver, `ACCEL` is the unsigned, smoothed radial acceleration of the
nearest tracked person. Its displayed value is amplified fivefold so normal
human starts, stops, and turns produce a more visible range.
