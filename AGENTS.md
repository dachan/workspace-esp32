# ESP32 workspace instructions

## Project scope

This repository contains ESP32-S3 firmware for an LD2450 radar display pair,
shared peripheral drivers, a WS2812B demo, and a portable virtual pet. The
radar application is in `radar/`; shared non-radar drivers are in
`hardware-test/`; the standalone LED demo is in `rainbow-wave/`; and the
virtual pet is in `super-tamagotchi/`.

Read the relevant project README and `super-tamagotchi/WIRING.md` before
changing firmware or connecting hardware.


## Agent workflow (Mac first, then server)

Hard lanes for this repo:

- **Edit locally on the Mac** at `~/Development/workspace-esp32` (commit and
  push from the Mac). Do not treat Hetzner `/home/david` as a place to edit
  this codebase (`david` is for prod serving of other apps).
- After Mac changes land on GitHub, **pull on Hetzner codex**
  (`/home/codex/workspace-esp32`) so the server clone stays in sync. Prefer
  connecting as `codex` for any server-side browse/write of this tree.
- When the user asks to **flash**, use the **Mac workstation clone** and the
  connected serial device there. Do not flash from Hetzner.
- Do not keep a separate standalone `mac-chatgpt-bridge` folder on the Mac;
  use `chatgpt-model-display/mac-chatgpt-bridge/` inside this repo.
- Keep Mac and server on the same branch/commit after every change:
  edit on Mac → commit/push → pull on Hetzner codex (and flash from Mac when
  hardware work is requested).

## mac-chatgpt-bridge

`chatgpt-model-display/mac-chatgpt-bridge/` is a macOS CLI. Foreground is
`NSWorkspace.frontmostApplication` (`com.openai.chat` / `com.openai.codex`
only — not Cursor). It never activates ChatGPT and never walks the AX tree.
Encoder `SET MODEL` / `SET THINKING` lines are applied with keyboard shortcuts
only while ChatGPT is already focused; otherwise they stay queued. On the
ChatGPT foreground edge the helper flushes the queue: Control-Shift-M, Up to
GPT-6 Astra, Down to the ESP dial index, Return; then absolute reasoning
(Ctrl+Shift+, clamp to Light, Ctrl+Shift-. up to target). Firmware waits
0.4 s after the last rotary detent before sending SET.
The helper only runs on the Mac.
On-device UI lives in `chatgpt-model-display/` (3.5\" ST7796 480x320). Edit
and flash on the Mac; pull Hetzner codex after push. Firmware keeps the last
model/thinking in NVS for boot.

### chatgpt-model-display locked view mapping

Desk pose (photo reference): glass left of breadboard, pins toward the ESP32,
USB toward the bottom of the frame. UI must read upright in that pose
(ChatGPT title at top of glass).

Locked ST7796 settings in `chatgpt-model-display/main/display.c`:

- `invert_color(true)`, RGB, SPI 26 MHz
- `swap_xy(true)`, `mirror(true, true)` plus the internal-RAM soft-180 band
  blit in `display_flush()` for desk pose (title top-left, version
  bottom-right). This is **not** the same as
  `hardware-test` `DISPLAY_PROFILE_ST7796U_3_5` (`mirror(false, true)`).
- Never reverse the PSRAM framebuffer in place. That races SPI DMA and
  corrupts the title strip.

Do not flip only one MADCTL mirror to “fix” rotation (glyphs mirror). Do
not remove the internal-RAM band blit without desk verification. Keep
`HARDWARE.md` in sync when this changes.

## Hardware inventory (keep current)

Always track the **latest hardware on each ESP32** in this workspace. Before
flashing or changing firmware, read [HARDWARE.md](HARDWARE.md). After any
hardware change, reflash, or newly identified USB serial device, update that
file in the same change set (or immediately after).

Record per board (one section each):

- Stable label (e.g. radar-transmitter, radar-receiver, super-tamagotchi, chatgpt-bridge)
- MCU / module and display (size, controller) plus touch if present
- Sensors / peripherals and non-obvious wiring (point at project WIRING.md when detailed)
- Firmware currently expected on the device (app path + role)
- Mac USB serial identity when known (e.g. `/dev/cu.usbmodem…`) — do not commit secrets; device paths are OK
- Last verified date

Do not invent boards. If unknown, mark **unknown** and fill in when discovered.

## Safety and public-repository hygiene

- Verify the intended board and serial device before flashing.
- Prefer `app-flash` for ordinary radar firmware updates when supported by the
  installed tooling.
- Do not commit credentials, device NVS dumps, full-flash images, generated
  configuration, local paths, or hardware-specific identifiers.
- Keep Wi-Fi credentials and other secrets in ignored local configuration or
  device storage.
- Do not write or run tests unless the user explicitly requests it. Firmware
  builds and live serial checks are allowed when needed for the requested work.

## Radar application

The transmitter reads LD2450 target frames over UART, broadcasts them with
ESP-NOW, and drives an external WS2812B motion indicator. The receiver renders
the radar view and receives frames over ESP-NOW. The receiver and transmitter
should remain visually and behaviorally equivalent where their different panel
sizes allow.

The LD2450 uses 5 V power, 3.3 V UART logic, and a UART connection with the
sensor TX line connected to the ESP32 RX GPIO and the sensor RX line connected
to the ESP32 TX GPIO. The driver probes supported baud rates and treats a valid
target frame as healthy radar data even if a configuration command times out.

The radar UI uses a black-and-green palette, a proportional -60 to +60 degree
fan, a visual-left distance readout, an edge-aligned coordinate readout, and a
fading detected-person marker. After an inactivity timeout the display sleeps
while the ESP32 continues listening; a new detection or supported touch action
redraws it.

The receiver's FT6336 touch controller is wired to SDA GPIO6, SCL GPIO15,
reset GPIO7, and INT GPIO5. A three-second hold while the display is awake
opens calibration; the hold must be released before selecting SLEEP or POWER
OFF. Receiver SLEEP and POWER OFF are both tap-to-wake deep-sleep modes. Keep
the touch reset high and backlight low during either mode; POWER OFF also holds
the LCD reset low because the receiver has no accessible EN switch.

## Desk control (encoders → ChatGPT)

Firmware `v 0.35+` updates the panel/NVS immediately, then sends
`SET MODEL <name>` / `SET THINKING <level>` 0.4 s after the last detent.
The Mac helper applies those with keyboard shortcuts only while ChatGPT is
already the foreground app: Control-Shift-M, Up to Astra, Down to the dial
index, Return; absolute Light clamp then Control-Shift-. for thinking.
Run the Mac bridge with serial listen + watch:

```bash
chatgpt-bridge --watch --send-serial --port /dev/cu.usbmodem21201
```

Requires Accessibility for the launching app (key posting). Foreground
detection does not. Thinking uses a polled falling-CLK decode; the model knob
uses PCNT hardware quadrature (a polled decode misreads it, because the display
flush delays the poll past the CLK/DT phase difference and the dial parks on one
end). Model clamps GPT-6 Astra through GPT-5.5 (no wrap);
thinking clamps Light ↔ Extra High.
If ChatGPT is not focused, the bridge must not activate it. Encoder changes
remain on the ESP32 display/NVS and are queued until ChatGPT returns to the
foreground, when the latest model and thinking settings are applied via
Control-Shift-M (Up park + Down) and absolute Ctrl+Shift+, / Ctrl+Shift-..

## Build and flash

Use a local ESP-IDF installation without embedding its path in scripts or
documentation:

```sh
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
cd radar

# Transmitter
idf.py -B build-radar-transmitter \
  -D RADAR_LINK_ROLE=transmitter build
ESP_PORT=/dev/ttyUSB0 ./tools/flash-radar.sh transmitter

# Receiver
idf.py -B build-radar-receiver-accel \
  -D RADAR_LINK_ROLE=receiver build
ESP_PORT=/dev/ttyUSB1 ./tools/flash-radar.sh receiver
```

The flash scripts require both `IDF_PATH` (or `IDF_EXPORT`) and `ESP_PORT` so
that no machine-specific installation path or device identity is stored in the
repository.

## Super Tamagotchi

This portable ESP32-S3 virtual pet combines a display, capacitive touch, and
speaker/amplifier audio. The desktop simulator renders the same creature code
without hardware. Use [super-tamagotchi/WIRING.md](super-tamagotchi/WIRING.md)
as the authoritative pin map, keep logic signals at 3.3 V, and disconnect power
before changing wiring.

Build and flash it with a local ESP-IDF installation:

```sh
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
cd super-tamagotchi
idf.py set-target esp32s3
idf.py build flash monitor
```

Choose the serial device through ESP-IDF options or an explicit local
environment variable. Do not hard-code machine-specific ports or paths. The
platform-neutral simulator can be run with `./sim/run.sh` or `./sim/run.sh
--live`; live mode serves frames on localhost only.

Keep rendering, hardware drivers, and application state in focused modules;
prefer small functions and straightforward control flow; and document
non-obvious hardware constraints in comments or wiring docs. Generated
binaries, simulator output, `sdkconfig`, environment files, credentials, audio
recordings, device dumps, and other private data must not be committed.

## Wiring documentation

When documenting a user-supplied module diagram, preserve its physical header
numbering and order. Use a table for direct peripheral-to-board wiring, pair
each signal with its ESP32 power rail or GPIO, and mark deliberately unused
connections as `NC`. Default to a monospaced text diagram unless an image is
explicitly requested. Do not silently substitute a different board pinout.

For a two-row peripheral header, use five columns in physical row order with a
blank separator column:

`peripheral input | ESP32 pin |  | ESP32 pin | peripheral input`

Order each side from the peripheral outward: list connector inputs in their
physical or functional order, followed by the matched ESP32 GPIO or power pin.
Prefer consecutive ESP32 GPIOs that follow the peripheral's pin order when the
board and fixed firmware requirements allow it. Keep one peripheral together
on the first available side; do not split it across the separator until that
side is full.

For a direct peripheral-to-board map, use one continuous
`peripheral | GPIO/power` pair in connector order. Do not place a second
peripheral sequence alongside it; list unused GPIO coverage in additional rows
or a separate audit table. Include every GPIO exposed on the pictured board,
marking unused connections as `NC`.

When documenting a peripheral with a physical multi-row header, include two
distinct diagrams: the physical-header layout above, plus a second sequential
diagram ordered by the peripheral's connector/pin order. The second diagram
must show each peripheral pin paired with its ESP32 GPIO or power connection;
do not reorder it to match visual placement on the board.
