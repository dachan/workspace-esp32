# ESP32 workspace instructions

## Project scope

This repository contains ESP32-S3 firmware for an LD2450 radar display pair,
shared peripheral drivers, a WS2812B demo, and a portable virtual pet. The
radar application is in `radar/`; shared non-radar drivers are in
`hardware-test/`; the standalone LED demo is in `rainbow-wave/`; and the
virtual pet is in `super-tamagotchi/`.

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

## AI model control bridge

`InputGuard.swift` protects each complete model/effort apply with a process-scoped
active event tap. Keep bridge keys tagged in `Keys.swift`; user input is discarded,
never queued. Do not post keys if the filter cannot start. Wait for held keys/buttons
before acquisition, preserve serial supersession, and release on every exit. Escape,
focus loss, disabled taps, and the independent five-second watchdog cancel the target.
The `--check-input-guard` diagnostic acquires/releases without posting keys. A build
or availability check does not verify physical input suppression.

`ai-model-control/mac-chatgpt-bridge/` is a macOS CLI. Foreground is
`NSWorkspace.frontmostApplication` (`com.openai.chat` / `com.openai.codex`
or Cursor `com.todesktop.230313mzl4w4u92` or OpenCode `ai.opencode.desktop`). It never activates those apps.
Before posting shortcuts it focuses the prompt: Cursor Command-L only if
Agents is not already open (Cmd+L is Toggle Sidepanel and would close it),
otherwise AX-focus `aislash-editor-input`; ChatGPT/Codex by message-box
identity — never by screen coordinates. Encoder `SET MODEL` / `SET THINKING` lines are
applied with that app's keyboard shortcuts only while it is already focused;
otherwise they are dropped and never applied later. ChatGPT apply: Control-Shift-M, Down to the ESP
dial index, Return; then absolute reasoning (Ctrl+Shift+, clamp to Light,
Ctrl+Shift-. up to target). Cursor apply: Command-/ (first Down is Auto),
or Left, Up, Right directly into Reasoning after reopening (Right highlights
the first supported level; Down to the target; Return selects, then Escape twice closes the menus). A
ChatGPT-only model name (e.g. GPT-6 Astra) is skipped while
Cursor is focused so effort can still apply. The helper sends `FRONT Cursor`, `FRONT ChatGPT`, `FRONT OpenCode`, or `FRONT None`. Cursor
and ChatGPT select that app's catalog and header lockup. OpenCode has its own
fixed dial, in provider-menu order: GPT-5.6 Luna, GPT-5.6 Sol, GPT-5.6 Terra,
and GPT-6 Astra. Keep firmware `opencode_models` and Swift `openCodeModels` aligned.
`FRONT None` keeps
the last app's catalog and logo; after 1 min without focus or encoder/touch
the panel shows a date/time screensaver. Focus, a knob, or a tap wakes it;
a waking tap does not hit SYNC/MODELS.
Firmware waits 0.4 s after the last rotary detent before sending SET; the
bridge settles 1 s more, then applies only changed fields (both in one pass
when both changed — a thinking-only change skips model selection).
The helper only runs on the Mac.
On-device UI lives in `ai-model-control/` (3.5" ST7796 480x320). Edit
on Hetzner codex, push, pull the Mac, then flash on the Mac. Firmware keeps the last
model/thinking in NVS for boot, the last model per app, and the last thinking
level per app+model. Empty NVS (first flash) defaults ChatGPT to GPT-5.6 Luna
Extra High and Cursor to Cursor Grok 4.6 Extra High. Switching ChatGPT ↔ Cursor
restores that app's last model and effort. A SYNC tap sends PUSH plus new STATE
revisions so the helper reapplies even if it already posted those values.
In Cursor mode, MODELS opens a saved enable list (Auto always on; default is
Cursor Grok 4.6, Composer 2.5, Claude Opus 5, GPT-5.6 Sol, Claude Fable 5,
GPT-5.6 Terra, GPT-5.6 Luna). The helper uses `ENABLED` so Command-/ Down
counts match that list.

### ai-model-control locked view mapping

Desk pose (photo reference): glass left of breadboard, pins toward the ESP32,
USB toward the bottom of the frame. UI must read upright in that pose
(focused app title at top of glass).

Locked ST7796 settings in `ai-model-control/main/display.c`:

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

The header brand lockup is an 8-bit coverage mask in `main/logo.c`, generated
from `assets/` by `scripts/generate_logos.py` (needs Pillow) and committed.
Coverage is luminance times alpha so the Cursor cube keeps its shaded faces.
Both logos render at the script's single `LOGO_HEIGHT` and share a baseline, so
the header footprint is identical whichever app is focused; keep it that way.
Re-run the script and commit `main/logo.c` after changing the artwork or that
height; do not hand-edit the generated file.

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

## AI model control (encoders → ChatGPT / Cursor / OpenCode)

Firmware `v 0.35+` updates the panel/NVS immediately, then sends
`SET MODEL <name>` / `SET THINKING <level>` 0.4 s after the last detent.
The Mac helper applies those with app-specific controls only while ChatGPT,
Cursor, or OpenCode is already the foreground app. ChatGPT: Control-Shift-M, Down to the
dial index, Return; absolute Light clamp then Control-Shift-. for thinking.
Cursor: Command-/ (first Down is Auto); effort is Left, Up, Right, then Down to the level.
A ChatGPT-only model name is skipped while Cursor is focused. The
bridge settles 1 s after the last received change and applies only fields
that differ from its last apply to that app; effort-only changes skip model selection.
Run the Mac bridge with serial listen + watch:

```bash
chatgpt-bridge --watch --send-serial --port /dev/cu.usbmodem21201
```

Requires Accessibility for the launching app (key posting and prompt focus). Foreground
detection does not. Thinking uses a polled falling-CLK decode and holds
pulses until the knob pauses (two detents = one level; a quick turn can
run Light↔Extra High). The model knob uses PCNT hardware quadrature (a
polled decode misreads it, because the display flush delays the poll past
the CLK/DT phase difference and the dial parks on one end). Model clamps
GPT-6 Astra through GPT-5.5 on ChatGPT, Auto then the enabled Cursor MODELS list
(no wrap); thinking clamps Light ↔ Extra High or that Cursor model's effort range.
If none of ChatGPT, Cursor, or OpenCode is focused, the bridge must not activate them.
Encoder changes remain on the ESP32 display/NVS and the bridge drops them;
there is no queue, no CANCEL button, and no deferred apply when one of those
apps returns to the foreground. Tap SYNC on the glass to push the current
panel model and thinking to the focused app. Focus lost mid-apply drops the
change as well;
only a failed or superseded attempt retries, and only while that app stays
focused. ChatGPT applies via Control-Shift-M
(Down from Astra) and absolute Ctrl+Shift+, / Ctrl+Shift-.; Cursor applies
via Command-/ (first Down is Auto) and Left, Up, Right then Down to the level.
A five-second press-and-hold starts a five-point touch calibration.

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

### Cursor effort capabilities

Right highlights the first supported effort; use its zero-based menu index for
Down presses, then Return once, then Escape twice to close the menus. Per-model
ranges live in `ai-model-control/main/catalog.c` (keep Swift `Catalog.swift`
aligned). Auto, Composer 2.5, and several Claude/Gemini/GPT/Kimi rows have no
effort support. Cursor Grok 4.6 is Low–Extra High; Claude Opus 5 / Fable 5 add
Max; GPT-5.6 Sol/Terra/Luna add None and Max. Gemini 3.6 Flash starts at
Minimal. Model changes must reapply effort because Cursor can restore its own
per-model value. Unsupported endpoints clamp to the supported range. Firmware
shows Unsupported and ignores effort rotation for models without reasoning;
None is a selectable GPT effort, not a synonym for unsupported. Command-/
apply uses catalog index; Cursor's picker skips models toggled off in Settings.
