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
Mac folder).

Firmware `v 0.35+` updates panel/NVS immediately, then sends
`SET MODEL <name>` / `SET THINKING <level>` **0.4 s** after the last rotary
detent. The bridge settles **0.25 s** more after the last received change, then
applies only fields that differ from its last apply to that app (both in one
pass when both changed — thinking-only skips model selection).

Model picker and confirmation timing is **0.25 s**; every bridge-posted
keystroke is separated by **0.05 s** across ChatGPT, Cursor, and OpenCode.
The firmware's 0.4 s encoder settle remains unchanged.

Foreground is `NSWorkspace.frontmostApplication` (`com.openai.chat` /
`com.openai.codex`, Cursor `com.todesktop.230313mzl4w4u92`, OpenCode
`ai.opencode.desktop`). The helper **never activates** those apps. Encoder
lines apply with that app's shortcuts **only while already focused**;
otherwise they are dropped — no queue, no CANCEL, no deferred apply when
focus returns. Focus lost mid-apply drops the change; only a failed or
superseded attempt retries, and only while that app stays focused. Tap SYNC
to send PUSH plus new STATE revisions so the helper reapplies even if it
already posted those values.

```bash
chatgpt-bridge --watch --send-serial --port /dev/cu.usbmodem21201
```

Requires Accessibility for the launching app (key posting and prompt focus).
Foreground detection does not.

**InputGuard.** `InputGuard.swift` protects each complete model/effort apply
with a process-scoped active event tap. Keep bridge keys tagged in
`Keys.swift`; user input is discarded, never queued. Do not post keys if the
filter cannot start. Wait for held keys/buttons before acquisition, preserve
serial supersession, and release on every exit. Escape, focus loss, disabled
taps, and the independent five-second watchdog cancel the target. The
`--check-input-guard` diagnostic acquires/releases without posting keys. A
build or availability check does not verify physical input suppression.

**Prompt focus.** Before posting shortcuts: Cursor Command-L only if Agents
is not already open (Cmd+L toggles the sidepanel and would close it);
otherwise AX-focus `aislash-editor-input`. ChatGPT/Codex by message-box
identity — never by screen coordinates.

**Per-app apply.**

- **ChatGPT:** Control-Shift-M, Down to the ESP dial index, Return; then
  absolute reasoning (Ctrl+Shift+, clamp to Light, Ctrl+Shift-. up to
  target). Model clamps GPT-6 Astra through GPT-5.5 (no wrap).
- **Cursor:** Command-/ (first Down is Auto); effort is Left, Up, Right into
  Reasoning after reopening (Right highlights the first supported level;
  Down to the target; Return; Escape twice closes menus). Model order is
  Auto then the enabled MODELS list (no wrap). A ChatGPT-only model name
  (e.g. GPT-6 Astra) is skipped while Cursor is focused so effort can still
  apply.
- **OpenCode:** fixed encoder order GPT-6 Astra, GPT-5.6 Terra, GPT-5.6 Sol,
  GPT-5.6 Luna. Command-apostrophe picker opens with Luna selected: Down
  3/2/1/0, then Return. Thinking stays local to the panel and is never
  synchronized. Keep firmware `opencode_models` and Swift `openCodeModels`
  aligned.

The helper sends `FRONT Cursor`, `FRONT ChatGPT`, `FRONT OpenCode`, or
`FRONT None`. Cursor and ChatGPT select that app's catalog and header lockup.
`FRONT None` keeps the last app's catalog and logo; after 1 min without focus
or encoder/touch the panel shows a date/time screensaver. Focus, a knob, or a
tap wakes it; a waking tap does not hit SYNC/MODELS.

**Encoders and NVS.** Thinking uses a polled falling-CLK decode and holds
pulses until the knob pauses (two detents = one level; a quick turn can run
Light↔Extra High). The model knob uses PCNT hardware quadrature — a polled
decode misreads it because the display flush delays the poll past the CLK/DT
phase difference and the dial parks on one end. Thinking clamps Light ↔
Extra High or that Cursor model's effort range.

Firmware keeps last model/thinking in NVS for boot, last model per app, and
last thinking per app+model. Empty NVS defaults ChatGPT to GPT-5.6 Luna Extra
High and Cursor to Cursor Grok 4.6 Extra High. Switching ChatGPT ↔ Cursor
restores that app's last model and effort. In Cursor mode, MODELS opens a
saved enable list (Auto always on; default Cursor Grok 4.6, Composer 2.5,
Claude Opus 5, GPT-5.6 Sol, Claude Fable 5, GPT-5.6 Terra, GPT-5.6 Luna). The
helper uses `ENABLED` so Command-/ Down counts match that list. A five-second
press-and-hold starts five-point touch calibration.

**Locked ST7796 view mapping.** Desk pose: glass left of breadboard, pins
toward the ESP32, USB toward the bottom of the frame. UI must read upright
(focused app title at top of glass). Locked settings in
`ai-model-control/main/display.c`: `invert_color(true)`, RGB, SPI 26 MHz;
`swap_xy(true)`, `mirror(true, true)` plus the internal-RAM soft-180 band
blit in `display_flush()` (title top-left, version bottom-right). This is
**not** the same as `hardware-test` `DISPLAY_PROFILE_ST7796U_3_5`
(`mirror(false, true)`). Never reverse the PSRAM framebuffer in place (races
SPI DMA, corrupts the title strip). Do not flip only one MADCTL mirror to
“fix” rotation (glyphs mirror). Do not remove the band blit without desk
verification. Keep `HARDWARE.md` in sync when this changes.

The header brand lockup is an 8-bit coverage mask in `main/logo.c`, generated
from `assets/` by `scripts/generate_logos.py` (needs Pillow) and committed.
Coverage is luminance × alpha so the Cursor cube keeps shaded faces. Both
logos share one `LOGO_HEIGHT` and baseline (identical header footprint).
Re-run the script and commit `main/logo.c` after artwork or height changes;
do not hand-edit the generated file.

**Cursor effort.** Right highlights the first supported effort; use its
zero-based menu index for Down presses, then Return, then Escape twice.
Per-model ranges live in `ai-model-control/main/catalog.c` (keep Swift
`Catalog.swift` aligned) — including which rows have no effort, Max/None/Minimal
extensions, and clamp behavior. Model changes must reapply effort (Cursor can
restore its own per-model value). Firmware shows Unsupported and ignores effort
rotation for models without reasoning; None is a selectable GPT effort, not a
synonym for unsupported. Command-/ apply uses catalog index; Cursor's picker
skips models toggled off in Settings.

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

The radar UI uses a black-and-green palette, a proportional -60 to +60 degree
fan, a visual-left distance readout, an edge-aligned coordinate readout, and a
fading detected-person marker. After inactivity the display sleeps while the
ESP32 keeps listening; a new detection or supported touch action redraws it.

Receiver FT6336 touch: SDA GPIO6, SCL GPIO15, reset GPIO7, INT GPIO5. A
three-second hold while awake opens calibration; release the hold before
selecting SLEEP or POWER OFF. Both are tap-to-wake deep-sleep modes. Keep touch
reset high and backlight low during either; POWER OFF also holds LCD reset low
(no accessible EN switch on the receiver).

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
(live serves frames on localhost only). Keep rendering, drivers, and app state
in focused modules; document non-obvious hardware constraints in comments or
wiring docs. Do not commit generated binaries, simulator output, `sdkconfig`,
env files, credentials, audio recordings, device dumps, or other private data.

## Wiring documentation

Preserve the user-supplied module's physical header numbering and order. Use a
table for direct peripheral-to-board wiring; pair each signal with its ESP32
power rail or GPIO; mark unused connections `NC`. Default to monospaced text
unless an image is requested. Do not silently substitute a different board
pinout.

Two-row peripheral header — five columns in physical row order with a blank
separator column:

`peripheral input | ESP32 pin |  | ESP32 pin | peripheral input`

Order each side from the peripheral outward (connector inputs, then matched
ESP32 GPIO/power). Prefer consecutive ESP32 GPIOs that follow the peripheral
pin order when board and firmware allow. Keep one peripheral together on the
first available side; do not split across the separator until that side is full.

Direct peripheral-to-board map: one continuous `peripheral | GPIO/power` pair
in connector order — do not place a second peripheral sequence alongside it;
list unused GPIO coverage in extra rows or a separate audit table. Include
every GPIO on the pictured board (`NC` for unused).

Multi-row header peripherals need two diagrams: physical-header layout, plus a
second sequential diagram in the peripheral's connector/pin order pairing each
pin with its ESP32 GPIO or power — do not reorder the second to match board
placement.
