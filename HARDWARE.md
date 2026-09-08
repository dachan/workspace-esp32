# ESP32 hardware inventory

Living list of boards used with this repo. Agents must keep this current
(see `AGENTS.md`). Prefer facts verified on the desk; mark unknowns.

Last inventory pass: 2026-09-08 (v0.88 OpenCode wordmark app-flash)

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
- Protocol: USB Serial/JTAG 115200, lines `MODEL <name>`; optional `THINKING <level>`
- Firmware currently on device: `ai-model-control/` **v 0.88** (OpenCode integration and supplied wordmark; existing clock screensaver and wake behavior)
- Display settings (LOCKED — title-strip noise fix 2026-09-07):
  - Controller: ST7796U, 480×320 landscape, SPI 26 MHz, `invert_color(true)`, RGB
  - MADCTL: `swap_xy(true)`, `mirror(true, true)` — desk 180 in hardware; do not flip only one mirror (glyphs)
  - Software 180 flush: ON via internal-RAM band blit + SPI transfer sync (v 0.22+)
  - Desk pose: display left of breadboard, header pins toward ESP32, USB toward bottom of frame → OpenAI/Cursor brand lockup at top of glass, MODEL left, version bottom-right
- Mac serial: `/dev/cu.usbmodem21201` (verify before flash)
- Last verified: 2026-09-08 — v0.88 application-only reflash on `/dev/cu.usbmodem21201` (hash-verified), preserving NVS. Board identity matched the recorded ESP32-S3. Live serial returned ENABLED and revisioned MODEL/THINKING state. Rebuilt Mac helper restarted and connected; foreground was loginwindow. Physical OpenCode wordmark and app-control behavior remain unverified; model CLK/DT wiring fault still open (see above).
- Notes: edit on Hetzner `/home/codex/workspace-esp32` as `codex`; push, pull Mac `~/Development/workspace-esp32`, flash on Mac
