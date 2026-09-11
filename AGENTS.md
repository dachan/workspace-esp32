# ESP32 workspace instructions

## Project scope

This repository contains ESP32-S3 firmware for an LD2450 radar display pair,
shared peripheral drivers, a WS2812B demo, and a portable virtual pet. Radar
is in `radar/`; shared non-radar drivers in `hardware-test/`; the LED demo in
`rainbow-wave/`; the virtual pet in `super-tamagotchi/`.

Read the relevant project README and `super-tamagotchi/WIRING.md` before
changing firmware or connecting hardware.

## Agent workflow (Hetzner edit, Mac flash)

Hard lanes for this repo:

- **Edit on Hetzner as `codex`** at `/home/codex/workspace-esp32` (commit and
  push from the server). Do not treat Hetzner `/home/david` as a place to edit
  this codebase (`david` is for prod serving of other apps). Prefer connecting
  as `codex` for any server-side browse/write of this tree.
- After server changes land on GitHub, **pull on the Mac**
  (`~/Development/workspace-esp32`) so the workstation clone stays in sync.
- When the user asks to **flash** or run the Mac bridge helper, use the **Mac
  workstation clone** and the connected serial device there. Do not flash from
  Hetzner.
- Do not keep a separate standalone `mac-chatgpt-bridge` folder on the Mac;
  use `ai-model-control/mac-chatgpt-bridge/` inside this repo.
- Keep Mac and server on the same branch/commit after every change:
  edit on Hetzner codex → commit/push → pull on Mac (and flash from Mac when
  hardware work is requested).
- After a branch merges into `main`, delete it from local and remote
  (`git branch -d <branch>` and `git push origin --delete <branch>`). Keep
  `main` and any branch that still has unmerged commits. Do not leave merged
  feature branches around.

## AI model control (encoders → ChatGPT / Cursor / OpenCode)

On-device UI is `ai-model-control/` (3.5" ST7796 480x320). Edit on Hetzner
codex, push, pull the Mac, then flash on the Mac. The Mac helper is
`ai-model-control/mac-chatgpt-bridge/` (macOS CLI only; never a standalone
Mac folder). Detail for keystroke recipes, InputGuard, NVS defaults, ST7796
MADCTL lock, and logo codegen lives in `ai-model-control/README.md`.

Firmware settle **0.4 s** after the last rotary detent before sending
`SET`/`STATE`. The bridge settles **0.25 s** more after the last received
change, then applies only fields that differ. Model picker / confirmation
waits are **0.25 s**; every bridge-posted keystroke is separated by **0.05 s**.

The helper **never activates** ChatGPT, Cursor, or OpenCode. Encoder lines
apply only while that app is already focused; otherwise they are dropped —
no activate, no CANCEL, no deferred apply when focus returns. Focus lost
mid-apply drops the change. Tap SYNC to send PUSH plus new STATE revisions.

**Encoders.** Thinking uses polled falling-CLK decode (two detents = one
level). The model knob uses PCNT hardware quadrature — a polled decode
misreads it because display flush delays the poll past the CLK/DT phase
difference. Keep firmware `main/catalog.c` and Swift `Catalog.swift` aligned.
Desk pose and locked ST7796 view mapping: see `HARDWARE.md` and
`ai-model-control/README.md`.

```bash
chatgpt-bridge --watch --send-serial --port /dev/cu.usbmodem21201
```

Requires Accessibility for the launching app (key posting and prompt focus).

## Hardware inventory (keep current)

Always track the **latest hardware on each ESP32**. Before flashing or changing
firmware, read [HARDWARE.md](HARDWARE.md). After any hardware change, reflash,
or newly identified USB serial device, update that file in the same change set
(or immediately after). Record per board: stable label; MCU/module and display
plus touch if present; sensors/peripherals and non-obvious wiring (point at
project WIRING.md when detailed); firmware expected (app path + role); Mac USB
serial identity when known (device paths OK — no secrets); last verified date.
Do not invent boards. If unknown, mark **unknown** and fill in when discovered.

## Safety and public-repository hygiene

- Verify the intended board and serial device before flashing.
- Prefer `app-flash` for ordinary radar firmware updates when supported.
- Do not commit credentials, device NVS dumps, full-flash images, generated
  configuration, local paths, or hardware-specific identifiers.
- Keep Wi-Fi credentials and other secrets in ignored local configuration or
  device storage.
- Do not write or run tests unless the user explicitly requests it. Firmware
  builds and live serial checks are allowed when needed for the requested work.

## Radar application

The transmitter reads LD2450 target frames over UART, broadcasts them with
ESP-NOW, and drives an external WS2812B motion indicator. The receiver renders
the radar view over ESP-NOW. Keep receiver and transmitter visually and
behaviorally equivalent where panel sizes allow.

The LD2450 uses 5 V power, 3.3 V UART logic, with sensor TX → ESP32 RX GPIO and
sensor RX → ESP32 TX GPIO. The driver probes supported baud rates and treats a
valid target frame as healthy even if a configuration command times out.

Receiver FT6336 touch: SDA GPIO6, SCL GPIO15, reset GPIO7, INT GPIO5. A
three-second hold while awake opens calibration; release the hold before
selecting SLEEP or POWER OFF. Both are tap-to-wake deep-sleep modes. Keep touch
reset high and backlight low during either; POWER OFF also holds LCD reset low
(no accessible EN switch on the receiver). UI chrome (palette, fan, readouts)
is documented in `radar/README.md`.

## Model Dial desktop app

`ai-model-control/mac-chatgpt-bridge` includes `model-dial`, a macOS menu-bar
wrapper around the `chatgpt-bridge` executable. Keep the bridge CLI available
for diagnostics. The app discovers compatible USB serial paths, owns one bridge
child process, can register at user login, and flashes only the application
partition when the user explicitly selects an image and esptool. Do not claim
the unsigned local app is a distributable release: that needs code signing,
notarization, a bundled flasher, and signed firmware artifacts.

## v0.90 release freeze

Keep `ai-model-control/VERSION` at **0.90**. The user explicitly authorized
rebuilding and reflashing the event-driven serial recovery change while retaining
this version number. This exception does not authorize other firmware changes.
Firmware repeats `READY <16-hex boot id>` until SYNC. The bridge sends SYNC
on connection and READY, never periodically. ENABLED is sent for SYNC and
model-list changes; STATE retries until ACK. Retain the 30-second TIME refresh.

## Build and flash

Use a local ESP-IDF installation without embedding its path in scripts or docs:

```sh
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
cd radar

# Transmitter
idf.py -B build-radar-transmitter \
  -D RADAR_LINK_ROLE=transmitter build
ESP_PORT=/dev/ttyUSB0 ./tools/flash-radar.sh transmitter

# Super Mini transmitter (4 MB / quad PSRAM; UART GPIO4/GPIO5)
idf.py -B build-radar-transmitter-supermini \
  -D RADAR_LINK_ROLE=transmitter \
  -D RADAR_BOARD=supermini \
  -D SDKCONFIG=/absolute/path/to/build-radar-transmitter-supermini/sdkconfig \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.supermini build
ESP_PORT=/dev/ttyUSB0 ./tools/flash-radar.sh transmitter-supermini

# Receiver
idf.py -B build-radar-receiver-accel \
  -D RADAR_LINK_ROLE=receiver build
ESP_PORT=/dev/ttyUSB1 ./tools/flash-radar.sh receiver
```

Flash scripts require both `IDF_PATH` (or `IDF_EXPORT`) and `ESP_PORT` so no
machine-specific path or device identity is stored in the repository.

## Super Tamagotchi

Portable ESP32-S3 virtual pet with display, capacitive touch, and
speaker/amplifier audio. Desktop simulator renders the same creature code
without hardware. Use [super-tamagotchi/WIRING.md](super-tamagotchi/WIRING.md)
as the authoritative pin map; keep logic at 3.3 V; disconnect power before
changing wiring.

```sh
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
cd super-tamagotchi
idf.py set-target esp32s3
idf.py build flash monitor
```

Choose the serial device via ESP-IDF options or a local env var — do not
hard-code ports or paths. Simulator: `./sim/run.sh` or `./sim/run.sh --live`
(live serves frames on localhost only). Do not commit generated binaries,
simulator output, `sdkconfig`, env files, credentials, or device dumps.

## Wiring documentation

Preserve the module's physical header numbering and order. Use a table for
direct peripheral-to-board wiring; pair each signal with its ESP32 power rail
or GPIO; mark unused connections `NC`. Default to monospaced text unless an
image is requested. Do not silently substitute a different board pinout.

Two-row peripheral header — five columns in physical row order with a blank
separator: `peripheral input | ESP32 pin |  | ESP32 pin | peripheral input`.
Direct maps use one continuous `peripheral | GPIO/power` pair in connector
order. Multi-row headers need a physical layout plus a sequential connector-
order diagram. Details and examples live in project WIRING.md / README files.
