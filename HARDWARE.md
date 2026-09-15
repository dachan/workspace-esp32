# ESP32 hardware inventory

Living list of boards used with this repo. Agents must keep this current
(see `AGENTS.md`). Prefer facts verified on the desk; mark unknowns.

Last inventory pass: 2026-09-14 (ESP32 device-to-build matching)

## ESP32 device/build matching

Do not choose a build from the transient `/dev/cu.usbmodem*` number alone; macOS can renumber after replug. Match the live board by MAC/chip and visible wiring first, then flash the matching build:

| Canonical device label | Stable identity | Correct build | Do not confuse with |
|---|---|---|---|
| `ESP32_S3-35_tft_touch_480x320-AI_Model_Control` | MAC `28:84:85:44:1b:5c`, 16 MB / 8 MB PSRAM class, ST7796U panel | `ai-model-control/ESP32_S3-35_tft_touch_480x320-AI_Model_Control` | Mini 4 MB images |
| `ESP32_S3-13_tft_240x240+096_oled_128x64-Date_Time` | MAC `7c:4f:ad:ba:c8:98`, ESP32-S3 QFN56 rev v0.2, 16 MB flash / 8 MB PSRAM | `date-time-display/ESP32_S3-13_tft_240x240+096_oled_128x64-Date_Time` | 1.3-inch TFT / 0.96-inch OLED wiring not yet physically verified |
| `ESP32_MINI-128_tft_240x240-AI_Model_Control-Original` | MAC `90:da:72:73:5a:64`, old round-display wiring `RST/CS/DC/SDA/SCL -> GPIO10/11/9/8/12` | `ai-model-control/ESP32_MINI-128_tft_240x240-AI_Model_Control-Original` | New ordered-pin build |
| `ESP32_MINI-128_tft_240x240-AI_Model_Control-New` | MAC `d4:05:92:47:d5:1c`, ESP32-S3 QFN56 rev v0.2, 4 MB flash / 2 MB PSRAM, display wired `RST/CS/DC/SDA/SCL -> GPIO8/9/10/11/12` | `ai-model-control/ESP32_MINI-128_tft_240x240-AI_Model_Control-New` | Original round build |
| `ESP32_MINI-no_display-Radar_Sensor` | MAC `d4:05:92:47:d1:6c`, LD2450 UART on GPIO4/GPIO5 | `radar/ESP32_MINI-no_display-Radar_Sensor` | AI Model Control Mini images |

## Unassigned display modules

These modules are inventory items only: neither has been electrically connected,
identified by its controller IC, nor assigned to an ESP32. Do not treat the
similar-looking boards as interchangeable with the existing FT6336 radar panel
or the GC9A01 Mini panel.

| Inventory label | Observed from supplied back photo | Connection status |
|---|---|---|
| 2.8-inch TFT SPI 240x320 v1.2 (Photo 1) | Silkscreen `2.8 TFT SPI 240X320 V1.2`; microSD socket; TFT pins `VCC/GND/CS/RESET/DC/SDI(MOSI)/SCK/LED/SDO(MISO)`; additional `T_CLK/T_CS/T_DIN/T_DO/T_IRQ` header; SD pads `SD_CS/SD_MOSI/SD_MISO/SD_SCK` | Unconnected; TFT controller and whether a touch overlay is fitted are unverified. |
| 2.8-inch TFT 240x320 RGB v1.1 (Photo 2) | Silkscreen `2.8\" TFT 240X320RGB V1.1`; microSD socket; the same TFT, `T_*`, and SD pin labels as Photo 1 | Unconnected; TFT controller and whether a touch overlay is fitted are unverified. |

### ESP32-S3 Mini compatibility note

Both modules appear electrically compatible with an ESP32-S3 Super Mini at
3.3 V logic, subject to checking the module documentation before power is
applied. They are not drop-in replacements for the 1.28-inch GC9A01 panel:
they need a 240x320 TFT controller profile and five TFT signals (`CS`, `RESET`,
`DC`, `MOSI`, `SCK`), plus `MISO` if SD or the `T_DO` touch readback is used.
The touch header, if an overlay is fitted, needs a separate `T_CS` and `T_IRQ`
and shares SPI clock/data with the TFT. The current Mini AI Model Control
firmware supports only the GC9A01 round panel and intentionally has touch
disabled, so it cannot drive either module without a new firmware profile and
verified wiring. Do not connect its `T_*` pins to the radar receiver's FT6336
I2C pins; they are a different interface.

## ESP32_S3-13_tft_240x240+096_oled_128x64-Date_Time

- MCU: ESP32-S3 QFN56 rev v0.2, dual core, 40 MHz crystal, embedded 16 MB flash and 8 MB PSRAM class
- MAC: `7c:4f:ad:ba:c8:98`
- Displays: Estardyn 1.3-inch 240x240 SPI TFT (pictured ST7789, 7-pin `BLK/DC/RES/SDA/SCK/VCC/GND`) running a never-ending fluid-motion screensaver with a 32-step light-pastel gradient from each random HSV base tint toward a soft highlight, continuously morphing to a new target at least 115 RGB-distance units away, with each visual cycle 25% longer than the previous settings, with a stronger Gaussian blur and bottom-right FPS counter, plus 0.96-inch 128x64 I2C SSD1306 OLED with centered date/time
- Joystick: passive 8-pin switch module, `COM -> GND`; `UP/DOWN/LEFT/RIGHT/MID/SEL/RST -> GPIO13/14/15/16/17/18/4`
- Wiring: TFT `BLK -> 3V3`, `DC/RES/SDA/SCK -> GPIO10/8/11/12`, `VCC -> 3V3`, `GND -> GND`; no exposed CS; OLED `SDA/SCL -> GPIO6/7`; see [date-time-display/WIRING.md](date-time-display/WIRING.md)
- Firmware expected: `date-time-display/ESP32_S3-13_tft_240x240+096_oled_128x64-Date_Time`
- Mac USB serial: `/dev/cu.usbmodem5C940014881` (re-verify after replug; macOS may renumber native USB CDC ports)
- Last verified: 2026-09-14 — chip, 16 MB flash, and 8 MB PSRAM matched MAC `7c:4f:ad:ba:c8:98`; bootloader, partition table, and application from commit `c4c825e` each passed independent esptool verification. The OLED renders a centered `HH:MM` time above the centered date; serial status retains second precision, and both text lines follow an eight-position ±2-pixel shift pattern advancing every 30 minutes. Each random HSV pastel target gets a 32-step gradient with the restored 64/255 highlight lift; the next target colour is rejected until it is at least 115 RGB-distance units from the previous base, palette progression updates every 20 frames, and fluid phase advances at 2/5 phase units per frame, making each visual cycle 25% longer while retaining the same render cadence. The restored 80x80 fluid field and stronger Gaussian blur were previously measured at 42–49 FPS across repeated one-second serial windows. Application SHA-256 `89cdc8e1ba735a671819271ae0d44981f7cd047ee5c9f19cdbf53606d9c6eb53`. Post-reset serial reached the ESP-IDF boot sequence on the MAC-matched board; the 80 MHz TFT signal integrity and final panel appearance still need desk-side visual confirmation.
- Notes: the firmware sends an all-zero WS2812 frame on GPIO48 and holds GPIO21 low at boot to turn off common programmable indicators. A hardware power/USB LED, if fitted, is not firmware-controllable. The joystick is a passive active-low switch bank with `COM -> GND` and inputs on GPIO13/14/15/16/17/18/4; see [date-time-display/WIRING.md](date-time-display/WIRING.md). The pictured TFT's backlight is supplied from 3V3 through `BLK`; brightness is not firmware-controlled in this build.


## ESP32_MINI-no_display-Radar_Sensor

- MCU: ESP32-S3 (radar project)
- Display / role: LD2450 UART sensor + ESP-NOW transmitter; external WS2812B motion bar (GPIO1)
- Touch: none (typical)
- Firmware: `radar/` with `RADAR_LINK_ROLE=transmitter`
- Mac serial: unknown
- Notes: See radar docs; LD2450 is 5 V power / 3.3 V UART

## ESP32_S3-28_tft__touch_320x240-Radar_Receiver

- MCU: ESP32-S3 (radar project)
- Display: radar UI panel (2.8-inch class per README); SPI display
- Touch: FT6336 — SDA GPIO6, SCL GPIO15, reset GPIO7, INT GPIO5
- Firmware: `radar/` with `RADAR_LINK_ROLE=receiver`
- Mac serial: unknown
- Notes: SLEEP / POWER OFF deep-sleep behavior documented in AGENTS.md

## ESP32_S3-no_display-Rainbow_Wave

- MCU: ESP32-S3
- Peripherals: WS2812B bar on GPIO1
- Firmware: `rainbow-wave/`
- Mac serial: unknown

## ESP32_S3-28_tft__touch_320x240-Super_Tamitgotchi

- MCU: ESP32-S3 portable virtual pet
- Display / touch / audio: see [super-tamagotchi/WIRING.md](super-tamagotchi/WIRING.md)
- Firmware: `super-tamagotchi/`
- Mac serial: unknown

## ESP32_S3-35_tft_touch_480x320-AI_Model_Control

- MCU: ESP32-S3 (QFN56) rev v0.2, embedded 8MB PSRAM (AP_3v3), 40MHz XTAL — Lonely Binary N16R8-class
- MAC: 28:84:85:44:1b:5c
- Display: **3.5" TFT SPI 480x320 v1 (ST7796U)** — pins MOSI 8, DC 9, RST 10, CS 11, MISO 16, BL 17, SCK 18; SD_CS 4 held high
- Rotary encoders (×2 KY-040-style) on the **RIGHT** header of `s3-n16r8.jpeg` (left header is display SPI). Both `+`→3V3 / `GND`→GND (right-side GND pins OK):
  - **Thinking** — CLK GPIO41, DT GPIO40, SW GPIO39 (polled falling CLK; two detents per level; burst until pause so a quick turn can span Light↔Extra High; click = next)
  - **Model** — CLK GPIO1, DT GPIO2, SW GPIO42 (PCNT hardware quadrature, 8 counts / two detents + 160 ms gap per model step; clamps at GPT-6 Astra / GPT-5.5; click = next)
    - **Known desk fault (2026-09-07, unresolved):** GPIO1 and GPIO2 carry the
      same signal. A 1 kHz sample of both pins while turning showed matched
      edge counts, identical levels (`1/1` or `0/0`) at every sample, and a
      PCNT count that only ever increased. With no phase difference there is
      no direction to decode, so the dial walks to one end of the list and
      parks — reproduced identically under polled falling-CLK, both-edge,
      Gray-table, and PCNT decoding. Fix the wiring (DT on its own node,
      not shared with CLK) before touching the decode again. The thinking
      knob on GPIO41/40 shows independent counts, which is why it works.
  - Panel/NVS update immediately; `SET MODEL` / `SET THINKING` wait 0.4 s after the last detent
  - Changes persist in NVS, including last effort per app+model (firmware v0.67+)
  - Board pinout image: [`s3-n16r8.jpeg`](s3-n16r8.jpeg)
- Touch: FT6336 — SDA GPIO6, SCL GPIO15, reset GPIO7, INT GPIO5 (5 s hold starts five-point calibration, NVS `touch`/`c35_desk`)
- Protocol: USB Serial/JTAG 115200; READY boot announcement, SYNC snapshot, revisioned STATE/ACK, and ENABLED model mask.
- Firmware currently on device: `ai-model-control/` **v 0.90** (OpenCode encoder: GPT-6 Astra, GPT-5.6 Terra, GPT-5.6 Sol, GPT-5.6 Luna; its native picker is Luna-first and confirms with Return)
- Display settings (LOCKED — title-strip noise fix 2026-09-07):
  - Controller: ST7796U, 480×320 landscape, SPI 26 MHz, `invert_color(true)`, RGB
  - MADCTL: `swap_xy(true)`, `mirror(true, true)` — desk 180 in hardware; do not flip only one mirror (glyphs)
  - Software 180 flush: ON via internal-RAM band blit + SPI transfer sync (v 0.22+)
  - Desk pose: display left of breadboard, header pins toward ESP32, USB toward bottom of frame → OpenAI/Cursor brand lockup at top of glass, MODEL left, version bottom-right
- Mac serial: unknown (not attached this pass; `/dev/cu.usbmodem21201` is currently the second Super Mini)
- Last verified: 2026-09-10 — application-only reflash, hash-verified, preserving NVS and the **v0.90** version. The panel has no SYNC control; Model Dial owns Sync, ChatGPT effort, and Cursor model enable lists. Build from commit `fd2c5d2`; firmware SHA-256 `f8f31afe5ca9efabd5ee3440dc20769be1c1123da98393fe8577ed1f28d0b612`. Board identity matched the recorded ESP32-S3. Bridge launched with the configured masks; live serial received ENABLED and both state fields after reconnect. Physical touch behavior was not rechecked; the documented model-knob wiring fault remains unresolved.
- Notes: edit on Hetzner `/home/codex/workspace-esp32` as `codex`; push, pull Mac `~/Development/workspace-esp32`, flash on Mac

## ESP32_MINI-128_tft_240x240-AI_Model_Control-Original

- MCU: ESP32-S3FH4R2 Super Mini — 4 MB in-package quad flash, 2 MB in-package quad PSRAM; single-core board variant per supplier listing
- MAC: 90:da:72:73:5a:64
- Display: **1.28-inch round 240x240 GC9A01 SPI** — display-header order is `RST → CS → DC → SDA → SCL → GND → VCC`, wired to GPIO10 → GPIO11 → GPIO9 → GPIO8 (MOSI) → GPIO12 (SCK) → GND → 3V3; no MISO or separate BL connection
- Onboard indicator: GPIO48 WS2812 RGB LED; firmware sends an all-zero frame at boot and holds GPIO48 (and GPIO21) low so the data line cannot float
- Display settings: GC9A01 native 240×240, SPI 26 MHz, `invert_color(true)`, RGB; MADCTL `swap_xy(false)`, `mirror(false, false)`; software row reverse in `display_flush()` so glyphs read LTR (hardware 180 left letters backwards)
- Rotary encoders: same KY-040-style pair using the accessible outer headers — thinking CLK/DT/SW GPIO4/5/6; model CLK/DT/SW GPIO1/2/7; encoder + to 3V3 and grounds to GND. GPIO3 is left unused because it is a boot-strapping pin. Both knobs use PCNT (sampled on a 1 ms task) so the round-panel SPI flush cannot drop detents.
- Touch: none identified; round profile disables FT6336 initialization and uses the Model Dial helper's Sync command
- Firmware expected: original round-display image from `ai-model-control/ESP32_MINI-128_tft_240x240-AI_Model_Control-Original` (old display order); do not flash the New label unless this board is rewired to RST/CS/DC/SDA/SCL -> GPIO8/9/10/11/12.
- Mac USB serial: `/dev/cu.usbmodem21101` (re-verify if the CDC address changes after replug)
- Last verified: 2026-09-11 — application-only reflash of `ESP32_MINI-128_tft_240x240-AI_Model_Control-Original` v0.90 to `/dev/cu.usbmodem21101` (hash verified, NVS preserved). Chip matched ESP32-S3 QFN56 rev v0.2, 4 MB flash + 2 MB PSRAM, MAC `90:da:72:73:5a:64`. Firmware SHA-256 `c58ad39d99e8bb6f845b7881c144b311688f1c520d9aabf3bd73599829f56a66`. This image uses a black/grayscale round UI and screensaver, standard 5×7 thinking text at 1× scale in the same white as the model name, 24 px model-to-thinking spacing, 8 px thinking-bar segments with 8 px gaps and a 4 px corner radius, and holds the GPIO48 WS2812 data line low after an RGB-off frame.
- Notes: edit on Hetzner `/home/codex/workspace-esp32` as `codex`; push, pull Mac `~/Development/workspace-esp32`, flash on Mac

## ESP32_MINI-128_tft_240x240-AI_Model_Control-New

- MCU: ESP32-S3 QFN56 rev v0.2 — embedded 4 MB flash, embedded 2 MB PSRAM, dual core, 40 MHz XTAL
- MAC: d4:05:92:47:d5:1c
- Display / peripherals: external round SPI panel photographed; controller/model is not marked in the photo. The flashed firmware expects the 1.28-inch round GC9A01 panel and two rotary encoders documented in `ai-model-control/README.md`.
- Display header order from the supplied photo (top to bottom): `RST → CS → DC → SDA → SCL → GND → 3V3`; expected SuperMini connections for the new build are GPIO8 → GPIO9 → GPIO10 → GPIO11 → GPIO12 → GND → 3V3. Physical connection to this board remains unverified.
- Firmware: flashed image is `ai-model-control/ESP32_MINI-128_tft_240x240-AI_Model_Control-New` from commit `dc976c3`; display order RST/CS/DC/SDA/SCL -> GPIO8/9/10/11/12, Model encoder direction `-1`, and readable display rotation `180` degrees.
- Mac USB serial: `/dev/cu.usbmodem21201` (re-verify after replug; macOS may renumber native USB CDC ports)
- Last verified: 2026-09-13 — full reflash of `ESP32_MINI-128_tft_240x240-AI_Model_Control-New` from commit `dc976c3` to `/dev/cu.usbmodem21201` (bootloader, partition table, and application digests matched in a separate esptool verify). Chip matched ESP32-S3 QFN56 rev v0.2 with embedded 4 MB flash + 2 MB PSRAM, MAC `d4:05:92:47:d5:1c`. Firmware SHA-256 `c5262171c04939de9890d4baf3182b029eb87fc076fb15ed64e31e4f48dc03d8`. Boot serial repeated `READY 5ede18f6378879c1` after reset.
- Notes: USB Serial/JTAG board newly identified on this date. Do not assume it is wired like an older SuperMini; verify every external connection before applying the documented GC9A01/encoder map.

## ESP32_MINI-no_display-Radar_Sensor (verified hardware)

- MCU: ESP32-S3 (QFN56) rev v0.2 — embedded 4 MB flash (XMC), embedded 2 MB PSRAM (AP_3v3), 40 MHz XTAL; Super Mini class matching ESP32-S3FH4R2
- MAC: d4:05:92:47:d1:6c
- Display / role: LD2450 UART sensor + ESP-NOW transmitter; optional WS2812B motion bar on GPIO1
- LD2450 wiring: 5V→5V, GND→GND, TX→GPIO4 (UART RX), RX→GPIO5 (UART TX); GPIO3 unused
- Touch: none
- Firmware: `radar/ESP32_MINI-no_display-Radar_Sensor`; `RADAR_LINK_ROLE=transmitter`, `RADAR_BOARD=supermini`, `sdkconfig.defaults.supermini`
- Mac USB serial: `/dev/cu.usbmodem21201` (re-verify if the CDC address changes after replug)
- Last verified: 2026-09-11 — full flash of `ESP32_MINI-no_display-Radar_Sensor` to `/dev/cu.usbmodem21201` (hash verified). Chip matched ESP32-S3 QFN56 rev v0.2, 4 MB flash + 2 MB PSRAM, MAC `d4:05:92:47:d1:6c`. Firmware SHA-256 `eb56b7f5397b7bd595c860c1bca7c86b062827a329baeffb4ed6358f7df089ce`. Boot log showed `headless LD2450 ESP-NOW transmitter start (Super Mini)` then `ESP_ERR_NOT_FOUND` because the LD2450 was not attached yet.
- Notes: this board currently enumerates on the CDC path previously used by the 3.5" desk panel. Do not flash the 16 MB transmitter or ST7796 desk image here.
