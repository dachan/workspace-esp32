# ESP32 hardware inventory

Living list of boards used with this repo. Agents must keep this current
(see `AGENTS.md`). Prefer facts verified on the desk; mark unknowns.

Last inventory pass: 2026-09-10 (SuperMini GC9A01 hardware 180 app-flash)

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

## ai-model-control target (model / thinking display)

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
- Mac serial: `/dev/cu.usbmodem21201` (verify before flash)
- Last verified: 2026-09-10 — application-only reflash, hash-verified, preserving NVS and the **v0.90** version. The panel has no SYNC control; Model Dial owns Sync, ChatGPT effort, and Cursor model enable lists. Build from commit `fd2c5d2`; firmware SHA-256 `f8f31afe5ca9efabd5ee3440dc20769be1c1123da98393fe8577ed1f28d0b612`. Board identity matched the recorded ESP32-S3. Bridge launched with the configured masks; live serial received ENABLED and both state fields after reconnect. Physical touch behavior was not rechecked; the documented model-knob wiring fault remains unresolved.
- Notes: edit on Hetzner `/home/codex/workspace-esp32` as `codex`; push, pull Mac `~/Development/workspace-esp32`, flash on Mac

## ai-model-control supermini target (round model / thinking display)

- MCU: ESP32-S3FH4R2 Super Mini — 4 MB in-package quad flash, 2 MB in-package quad PSRAM; single-core board variant per supplier listing
- MAC: 90:da:72:73:5a:64
- Display: **1.28-inch round 240x240 GC9A01 SPI** — VCC 3V3, GND GND, SCK GPIO12, MOSI GPIO8, CS GPIO11, DC GPIO9, RST GPIO10; no MISO or separate BL connection
- Onboard indicator: GPIO48 WS2812 RGB LED; firmware sends an all-zero frame at boot and holds GPIO48 (and GPIO21) low so the data line cannot float
- Display settings: GC9A01 native 240×240, SPI 26 MHz, `invert_color(true)`, RGB; MADCTL `swap_xy(false)`, `mirror(false, false)`; software row reverse in `display_flush()` so glyphs read LTR (hardware 180 left letters backwards)
- Rotary encoders: same KY-040-style pair using the accessible outer headers — thinking CLK/DT/SW GPIO4/5/6; model CLK/DT/SW GPIO1/2/7; encoder + to 3V3 and grounds to GND. GPIO3 is left unused because it is a boot-strapping pin. Both knobs use PCNT (sampled on a 1 ms task) so the round-panel SPI flush cannot drop detents.
- Touch: none identified; round profile disables FT6336 initialization and uses the Model Dial helper's Sync command
- Firmware expected: `ai-model-control/` with `AI_MODEL_PROFILE=supermini`; `sdkconfig.defaults.supermini`; v0.90
- Mac USB serial: `/dev/cu.usbmodem21101` (re-verify if the CDC address changes after replug)
- Last verified: 2026-09-11 — application-only reflash of `build-supermini-1000hz` v0.90 to `/dev/cu.usbmodem21101` (hash verified, NVS preserved). Chip matched ESP32-S3 QFN56 rev v0.2, 4 MB flash + 2 MB PSRAM, MAC `90:da:72:73:5a:64`. Firmware SHA-256 `790532170ee0f70b75415971b82b1c3e8bbbbf06a69a821b17afc56e73fb5a44`. This image uses standard 5×7 thinking text at 2× scale in the same white as the model name, 24 px model-to-thinking spacing, 8 px thinking-bar segments with a 4 px corner radius, and holds the GPIO48 WS2812 data line low after an RGB-off frame.
- Notes: edit on Hetzner `/home/codex/workspace-esp32` as `codex`; push, pull Mac `~/Development/workspace-esp32`, flash on Mac
