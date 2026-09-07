# ESP32 hardware inventory

Living list of boards used with this repo. Agents must keep this current
(see `AGENTS.md`). Prefer facts verified on the desk; mark unknowns.

Last inventory pass: 2026-09-07 (chatgpt-model-display title-strip fix)

## radar-transmitter

- MCU: ESP32-S3 (radar project)
- Display / role: LD2450 UART sensor + ESP-NOW transmitter; external WS2812B motion bar (GPIO1)
- Touch: none (typical)
- Firmware: `radar/` with `RADAR_LINK_ROLE=transmitter`
- Mac serial: unknown
- Notes: See radar docs; LD2450 is 5 V power / 3.3 V UART

## radar-receiver

- MCU: ESP32-S3 (radar project)
- Display: radar UI panel (2.8-inch class per README); SPI display
- Touch: FT6336 — SDA GPIO6, SCL GPIO15, reset GPIO7, INT GPIO5
- Firmware: `radar/` with `RADAR_LINK_ROLE=receiver`
- Mac serial: unknown
- Notes: SLEEP / POWER OFF deep-sleep behavior documented in AGENTS.md

## rainbow-wave

- MCU: ESP32-S3
- Peripherals: WS2812B bar on GPIO1
- Firmware: `rainbow-wave/`
- Mac serial: unknown

## super-tamagotchi

- MCU: ESP32-S3 portable virtual pet
- Display / touch / audio: see [super-tamagotchi/WIRING.md](super-tamagotchi/WIRING.md)
- Firmware: `super-tamagotchi/`
- Mac serial: unknown

## chatgpt-bridge target (model / thinking display)

- MCU: ESP32-S3 (QFN56) rev v0.2, embedded 8MB PSRAM (AP_3v3), 40MHz XTAL — Lonely Binary N16R8-class
- MAC: 28:84:85:44:1b:5c
- Display: **3.5" TFT SPI 480x320 v1 (ST7796U)** — pins MOSI 8, DC 9, RST 10, CS 11, MISO 16, BL 17, SCK 18; SD_CS 4 held high
- Rotary encoders (×2 KY-040-style) on the **RIGHT** header of `s3-n16r8.jpeg` (left header is display SPI). Both `+`→3V3 / `GND`→GND (right-side GND pins OK):
  - **Thinking** — CLK GPIO41, DT GPIO40, SW GPIO39 (Light→Medium→High→Extra High; click = next)
  - **Model** — CLK GPIO1, DT GPIO2, SW GPIO42 (GPT-6 Astra through GPT-5.4 Mini; click = next)
  - Changes persist in NVS
  - Board pinout image: [`s3-n16r8.jpeg`](s3-n16r8.jpeg)
- Touch: not used by this firmware (FT6336 may be present)
- Protocol: USB Serial/JTAG 115200, lines `MODEL <name>`; optional `THINKING <level>`
- Firmware currently on device: `chatgpt-model-display/` **v 0.30**
- Display settings (LOCKED — title-strip noise fix 2026-09-07):
  - Controller: ST7796U, 480×320 landscape, SPI 26 MHz, `invert_color(true)`, RGB
  - MADCTL: `swap_xy(true)`, `mirror(true, true)` — desk 180 in hardware; do not flip only one mirror (glyphs)
  - Software 180 flush: ON via internal-RAM band blit + SPI transfer sync (v 0.22+)
  - Desk pose: display left of breadboard, header pins toward ESP32, USB toward bottom of frame → ChatGPT title at top of glass, MODEL left, version bottom-right
- Mac serial: `/dev/cu.usbmodem21201` (verify before flash)
- Last verified: 2026-09-07 — v0.30 flash and encoder→ChatGPT Extra High change verified; display band blit retained
- Notes: edit+flash on Mac `~/Development/workspace-esp32`; push then pull Hetzner `/home/codex/workspace-esp32`

