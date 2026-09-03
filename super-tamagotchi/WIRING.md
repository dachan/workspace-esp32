# Super Tamagotchi wiring

Current connector map for the ESP32-S3, ILI9341V/FT6336G capacitive display,
MAX98357 speaker amplifier, and future LSM6DS3 IMU.

> **Disconnect USB power before moving any wire.** All logic signals are 3.3V.
> The display is powered from 3.3V; only the MAX98357 `VIN` uses 5V.

## System diagram

```text
ESP32-S3
  |-- 3V3/GND + GPIO4, 8-11, 16-18 --> ILI9341V display
  |-- GPIO5-7, 15 -------------------> FT6336G capacitive touch
  |-- 5V/GND + GPIO38-40, 42 ---------> MAX98357 amplifier --> speaker
  `-- LSM6DS3 IMU --------------------> not connected
```

## ESP32-S3 expansion headers

Follow the labels printed on the expansion board rather than relying on board
orientation. Each signal is available in two parallel sockets on its labelled
column.

| Position | Side A | Side B |
|---:|---|---|
| 1 | `3V3` | `3V3` |
| 2 | `GND` | `5V` |
| 3 | GPIO4 | `GND` |
| 4 | GPIO5 | `GND` |
| 5 | GPIO6 | GPIO19 |
| 6 | GPIO7 | GPIO20 |
| 7 | GPIO15 | GPIO21 |
| 8 | GPIO16 | GPIO47 |
| 9 | GPIO17 | GPIO48 |
| 10 | GPIO18 | GPIO38 |
| 11 | GPIO8 | GPIO39 |
| 12 | GPIO3 | GPIO40 |
| 13 | GPIO9 | GPIO41 |
| 14 | GPIO10 | GPIO42 |
| 15 | GPIO11 | GPIO2 |
| 16 | GPIO12 | GPIO1 |
| 17 | GPIO13 | `GND` |
| 18 | GPIO14 | `GND` |
| 19 | `GND` | `5V` |
| 20 | `3V3` | `3V3` |

### Current GPIO use

| GPIO | Connected to | Purpose |
|---:|---|---|
| 4 | Display `SD_CS` | Hold unused microSD slot inactive |
| 5 | Display `CTP_INT` | Capacitive-touch interrupt |
| 6 | Display `CTP_SDA` | Capacitive-touch I2C data |
| 7 | Display `CTP_RST` | Capacitive-touch reset |
| 8 | Display `SDI(MOSI)` | Display MOSI |
| 9 | Display `DC` | Display data/command |
| 10 | Display `LCD_RST` | Display reset |
| 11 | Display `LCD_CS` | Display chip select |
| 15 | Display `CTP_SCL` | Capacitive-touch I2C clock |
| 16 | Display `SDO(MISO)` | Display MISO |
| 17 | Display `LED` | Backlight PWM |
| 18 | Display `SCK` | Display clock |
| 38 | MAX98357 `BCLK` | I2S bit clock |
| 39 | MAX98357 `LRC` | I2S word/left-right clock |
| 40 | MAX98357 `DIN` | I2S audio data to amplifier |
| 42 | MAX98357 `SD` | Amplifier enable/mute |

The new display occupies GPIO4-18 as one contiguous wiring block. The IMU is
not connected yet and must be assigned a new three-pin group before step 3.

## Display and touch connector

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

| Display module pin | ESP32-S3 connection | Notes |
|---|---|---|
| `VCC` | `3V3` | Do not use 5V for this build |
| `GND` | `GND` | Shared ground |
| `LCD_CS` | GPIO11 | Display chip select |
| `LCD_RST` | GPIO10 | Display reset |
| `LCD_RS (DC)` | GPIO9 | Data/command |
| `SDI(MOSI)` | GPIO8 | Display data from ESP32 |
| `SCK` | GPIO18 | Display clock |
| `LED` | GPIO17 | Active-high backlight PWM |
| `SDO(MISO)` | GPIO16 | Display data to ESP32 |
| `CTP_SCL` | GPIO15 | FT6336G I2C clock |
| `CTP_RST` | GPIO7 | Active-low touch reset |
| `CTP_SDA` | GPIO6 | FT6336G I2C data |
| `CTP_INT` | GPIO5 | Active-low touch interrupt |
| `SD_CS` | GPIO4 | Firmware holds high; SD is unused |

This display's microSD slot shares the LCD SPI lines. It remains unused, and
the firmware actively holds `SD_CS` high to prevent bus contention.

## MAX98357 amplifier and speaker

In the breakout's own silkscreen order:

| MAX98357 pin | Connection | Notes |
|---|---|---|
| `LRC` | GPIO39 | I2S word-select clock |
| `BCLK` | GPIO38 | I2S bit clock |
| `DIN` | GPIO40 | I2S audio data |
| `GAIN` | Not connected | Uses the breakout's default gain |
| `SD` | GPIO42 | Firmware hard-mutes the amp between sounds |
| `GND` | ESP32 `GND` | Must share ground with the ESP32 |
| `VIN` | ESP32 `5V` | Amplifier power |
| `OUT+` | Speaker `+` | Green screw terminal |
| `OUT-` | Speaker `-` | Green screw terminal; never connect to ground |

```text
ESP32-S3                         MAX98357                  Speaker
---------                        ---------                 -------
GPIO39 ------------------------> LRC
GPIO38 ------------------------> BCLK
GPIO40 ------------------------> DIN
                                  GAIN   (not connected)
GPIO42 ------------------------> SD
GND    ------------------------> GND
5V     ------------------------> VIN
                                  OUT+ -------------------> +
                                  OUT- -------------------> -
```

Never leave `VIN` disconnected while `BCLK`, `LRC`, `DIN`, or `SD` remain driven.
The amplifier can otherwise be phantom-powered through its signal inputs, causing
distortion and potentially damaging the hardware.

## LSM6DS3 IMU

Not connected yet. GPIO4-6 now belong to the new display, so the old IMU map
below is retired. Choose and document a new I2C/interrupt group when IMU
bring-up starts; `SDO` should still be tied to `GND` for address `0x6A`.

| LSM6DS3 pin | Connection | Notes |
|---|---|---|
| `VIN` | `3V3` | **3.3V only** — not 5V tolerant |
| `GND` | `GND` | Shared ground |
| `SCL` | Not assigned | I2C clock |
| `SDA` | Not assigned | I2C data |
| `INT1` | Not assigned | Interrupt (tap, wake, free-fall, etc.) |
| `SDO/SA0` | `GND` | Selects I2C address `0x6A` |
| `CS` | `3V3` | Ties the part into I2C mode |

```text
ESP32-S3                         LSM6DS3
---------                        -------
3V3    ------------------------> VIN
GND    ------------------------> GND
TBD    ------------------------> SCL
TBD    ------------------------> SDA
TBD    ------------------------> INT1
GND    ------------------------> SDO/SA0
3V3    ------------------------> CS
```

## Reserved and avoided pins

| GPIO | Status | Reason |
|---:|---|---|
| 3 | Do not use | Boot-strapping pin |
| 19, 20 | Reserved | Native USB data connection |
| 48 | Do not use | Onboard addressable RGB LED, confirmed by the board silkscreen |
| 21 | Free | Former display backlight pin |
| 41 | Reserved | Future microphone I2S data |
| 45, 46 | Not broken out / do not use | Boot-strapping pins |

GPIO45 is particularly unsuitable: its reset-time level selects the flash/PSRAM
voltage. Do not move a display signal to GPIO3, GPIO45, or GPIO46 merely to fill
an unused connector.
