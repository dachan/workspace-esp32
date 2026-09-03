# ESP32-S3 LD2450 radar

The sensor board is a headless transmitter: it reads LD2450 target frames over
UART, broadcasts them via ESP-NOW on Wi-Fi channel 6, and drives the external
WS2812B bar as a motion indicator. It does not initialize or require a display,
touch controller, joystick, microphone, speech model, or microSD card.

The separate receiver board renders the radar display. It has no joystick,
microphone, or voice controls. Its distance readout is followed by radial
acceleration; all receiver text is scaled to 80% of the former size.

## Wiring

| LD2450 signal | ESP32-S3 DEVKITC-1 |
|---|---|
| 5V | 5V |
| GND | GND |
| TX | GPIO41 (ESP32 UART receive) |
| RX | GPIO42 (ESP32 UART transmit) |
| WS2812B DIN | GPIO1 |

The LD2450 UART logic is 3.3 V. Its 5 V supply must support more than 200 mA.
The installed module produces valid target frames at 9600 baud; the firmware
probes its supported UART rates at startup.

## Build and flash

```sh
source $IDF_PATH/export.sh
cd <repo-root>/radar
idf.py -B build-radar-transmitter -D RADAR_LINK_ROLE=transmitter build
idf.py -B build-radar-transmitter -p /dev/ttyUSB0 app-flash

# Receiver/display board
idf.py -B build-radar-receiver-accel -D RADAR_LINK_ROLE=receiver build
idf.py -B build-radar-receiver-accel -p /dev/ttyUSB1 app-flash
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
and faster as acceleration increases.

On the receiver, `ACCEL` is the unsigned, smoothed radial acceleration of the
nearest tracked person. Its displayed value is amplified fivefold so normal
human starts, stops, and turns produce a more visible range.
