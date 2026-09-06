# ESP32 hardware inventory

Living list of boards used with this repo. Agents must keep this current
(see `AGENTS.md`). Prefer facts verified on the desk; mark unknowns.

Last inventory pass: 2026-09-06

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

- MCU: ESP32-S3 (USB serial from Mac `mac-chatgpt-bridge`)
- Display: unknown until confirmed on desk — reuse panel drivers from an existing project only after hardware match
- Protocol: USB serial 115200, lines `MODEL <name>`
- Firmware: TBD (`chatgpt-model-display/` or chosen app)
- Mac serial (last seen): `/dev/cu.usbmodem21201` (verify before flash — path can change)
- Notes: flash only from Mac `~/Development/workspace-esp32` after pull
