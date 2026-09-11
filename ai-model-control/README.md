# ai-model-control

ESP32-S3 AI model control panel for **model** and **thinking** selection across
ChatGPT, Cursor, and OpenCode
on the desk-mounted 3.5" ST7796U panel. Rotary encoders change both locally
(display + NVS). After **0.4 s** with no further changes the firmware sends
the latest state to `mac-chatgpt-bridge/`. The Mac helper
applies those only while ChatGPT, Cursor, or OpenCode is already the foreground app.
Changes are never deferred: state that arrives while no supported app is focused
is dropped by the helper and stays on the panel and in NVS only.

## Protocol (USB serial, 115200)

The current helper sends `SYNC` on connection and on `READY <16-hex boot id>`.
Firmware repeats READY every 0.5 seconds until SYNC, recovering even when
a reset does not disconnect USB. There is no periodic SYNC. Firmware
responds with its latest model and thinking after any active 0.4 s settle window:

```text
Mac → ESP: SYNC
ESP → Mac: ENABLED <16-hex Cursor enable mask>
Mac → ESP: CONFIG CHATGPT_EFFORTS <16-hex effort mask>
Mac → ESP: CONFIG CURSOR_MODELS <16-hex model mask>
ESP → Mac: STATE <16-hex revision> MODEL <name>
ESP → Mac: STATE <16-hex revision> THINKING <level>
Mac → ESP: ACK <16-hex revision> MODEL
Mac → ESP: ACK <16-hex revision> THINKING
ESP → Mac: STATE <16-hex revision> MODEL <name>
ESP → Mac: STATE <16-hex revision> THINKING <level>
```

The Model Dial helper's Sync menu item restarts the bridge, which requests the
panel's current MODEL/THINKING revisions and reapplies them to the focused app.

Each changed field gets a new revision, including after firmware restart.
Unacknowledged state retries every 0.5 s; a full USB transmit buffer retries after
0.2 s without blocking encoder polling. The helper acknowledges validated state
on receipt and ignores repeated revisions for application purposes.
Acknowledgement does **not** confirm the app's selected value: keyboard posting
has no UI readback. READY-triggered SYNC also recovers a device reset without requiring
the USB device path to disappear.

Mac also sends `TIME <unix-seconds> <tz-offset-minutes>` on connect and every
30 s so the panel can show a local clock. Firmware ticks minutes from that
snapshot; it does not use Wi-Fi or SNTP. The helper also sends `FRONT Cursor`,
`FRONT ChatGPT`, `FRONT OpenCode`, or `FRONT None` when the focused app changes so the
header brand lockup matches. `FRONT None` keeps the last app's catalog and
logo; it does not fall back to ChatGPT. After each SYNC the helper resends
FRONT so a firmware restart recovers focus. After 1 min without Cursor, ChatGPT, or OpenCode
focus and without encoder or touch, the panel shows a date/time screensaver.
Focusing Cursor, ChatGPT, or OpenCode, turning a knob, or tapping the glass wakes it.

A five-second press-and-hold anywhere on the glass starts a five-point touch
calibration. The Model Dial helper's **Sync** menu item asks the bridge to
apply the current panel model and thinking to the focused app. The Model Dial
Settings window controls which ChatGPT effort levels and Cursor models are
available on the encoders; the bridge sends those masks to the panel on every
connection. A release is confirmed after 150 ms without contact so a transient
FT6336 read error cannot create a false release.

Before the first `SYNC`, firmware uses the legacy `SET MODEL <name>` and
`SET THINKING <level>` lines. The current helper accepts these from older firmware,
but recovery/acknowledgements require both updated components. An old helper used
after a new helper negotiated SYNC requires a device restart to restore legacy TX.

Optional legacy display updates remain accepted as `MODEL <name>` and
`THINKING <level>`. MODEL can include a thinking suffix. Unsupported values are
ignored; local edits suppress these updates for 8.4 s (settle plus the original
8 s hold), while ACK and SYNC remain active. The current helper does not send
legacy display updates.

Frames are newline-delimited, bounded to 191 content bytes, and oversized frames
are discarded through the next newline. Transmissions include a leading newline
to recover framing after a disconnect mid-transfer.

Changed values are saved once per input pass to NVS (`cgpt`/`model`,`think`
for the last displayed pair, `cgpt`/`last_g` and `last_c` for each app's last
model, `cgpt`/`c_en` for the Cursor model mask, `cgpt`/`g_en` for the ChatGPT
effort mask, plus `cgpt`/`effort` for each app's last thinking level per model)
and reloaded on boot. Empty NVS (first flash) starts ChatGPT on GPT-5.6 Luna
Extra High and Cursor on Cursor Grok 4.6 Extra High. Changing models restores
that model's saved effort for the focused app; switching ChatGPT ↔ Cursor
restores that app's last model and effort. A first visit to a model keeps the
current level and clamps it. Unchanged values do not trigger persistence or
display work.

Dial models follow the focused app. ChatGPT: GPT-6 Astra, GPT-5.6 Sol,
GPT-5.6 Terra, GPT-5.6 Luna, GPT-5.5; thinking Light, Medium, High,
Extra High, Max, Ultra. Cursor: Auto, then the enabled model list (defaults: Cursor Grok 4.6,
Composer 2.5, Claude Opus 5, GPT-5.6 Sol, Claude Fable 5, GPT-5.6 Terra,
GPT-5.6 Luna). Effort depends on the model (see Cursor effort ranges below).
Command-/ apply uses this enabled index; keep the same models on in Cursor
Settings. Canonical names live in firmware `main/catalog.c` and Swift
`Sources/Catalog.swift`; keep these tables aligned when adding entries.

## Hardware

3.5" ST7796U SPI panel:

| Signal | GPIO |
|---|---:|
| MOSI | 8 |
| DC | 9 |
| RST | 10 |
| CS | 11 |
| MISO | 16 |
| Backlight | 17 |
| SCK | 18 |
| SD_CS (held high) | 4 |
| CTP_SCL | 15 |
| CTP_RST | 7 |
| CTP_SDA | 6 |
| CTP_INT | 5 |
| Thinking ENC CLK | 41 |
| Thinking ENC DT | 40 |
| Thinking ENC SW | 39 |
| Model ENC CLK | 1 |
| Model ENC DT | 2 |
| Model ENC SW | 42 |
| Both ENC + | 3V3 |
| Both ENC GND | GND |

USB: native USB Serial/JTAG (`/dev/cu.usbmodem*` on macOS). Flash and
`SET` traffic share that port.

### ESP32S3SuperMini round profile

The alternate `supermini` profile targets the ESP32-S3FH4R2 (4 MB quad flash,
2 MB quad PSRAM) and the 1.28-inch 240x240 GC9A01 round SPI panel shown in the
hardware inventory. It keeps the same rotary-encoder GPIOs and serial protocol
as the desk target, but uses a compact circular layout and disables touch.

Round-panel wiring:

| Signal | GPIO / rail |
|---|---:|
| GC9A01 SCL/SCK | 12 |
| GC9A01 SDA/MOSI | 8 |
| GC9A01 CS | 11 |
| GC9A01 DC | 9 |
| GC9A01 RST | 10 |
| GC9A01 VCC | 3V3 |
| GC9A01 GND | GND |
| Thinking encoder CLK / DT / SW | 4 / 5 / 6 |
| Model encoder CLK / DT / SW | 1 / 2 / 7 |
| Encoder + / GND | 3V3 / GND |

These assignments use only the outer through-hole headers. GPIO3 is deliberately
left unused because it is a boot-strapping pin; GPIO18 and GPIO39-42 are only
available on this board's small underside pads.

The round module has no MISO or separate backlight control in this wiring;
leave MISO unconnected and power its VCC from 3V3. Desk pose keeps native
MADCTL (`swap_xy(false)`, `mirror(false, false)`) and un-mirrors glyphs with
a horizontal row reverse in `display_flush()`. Do not use MADCTL `mirror(true,
true)` on this module — that 180 left letters backwards. Build it in its own
output directory so the legacy 480x320 profile remains intact:

```sh
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
cd ai-model-control
idf.py -B build-supermini \
  -D AI_MODEL_PROFILE=supermini \
  -D SDKCONFIG=/absolute/path/to/build-supermini/sdkconfig \
  -D SDKCONFIG_DEFAULTS=sdkconfig.defaults.supermini build
```

The round target has no touch calibration or glass SYNC control; use the
Model Dial helper's Sync command, and use encoder clicks for the existing local
thinking/model actions.

## Desk control (encoders → ChatGPT / Cursor)

Firmware `v 0.35+` updates the panel immediately, then sends SET after a
0.4 s settle window. Thinking pulses are held until the knob pauses so
two detents are one level and a quick turn can run Light↔Extra High. Model
steps still use a 160 ms emit gap. The Mac helper uses `NSWorkspace.frontmostApplication`
and focuses the prompt first: Cursor Command-L only if Agents is not already
open (it toggles the panel otherwise), otherwise AX-focus the composer;
ChatGPT by Accessibility element identity. While ChatGPT is focused it opens the visible model
control by Accessibility, chooses **Select model**, and presses the exact model label; it steps reasoning with Control-Shift-, / Control-Shift-.
While Cursor is focused it opens the model list with Command-/ (first Down
is Auto), then reopens it for Effort with Left, Up, Right, then Down-only to
the level; Return selects, then Escape twice closes the menus. The bridge
settles 0.25 s after the last received change, applies model and effort in one
pass using a single latest-target worker. New generations supersede older
operations. Fields are marked unknown before posting keys, so partial or
interrupted operations cannot suppress the final correction when a dial returns
to an earlier value. Model changes always invalidate effort; otherwise an
effort-only change skips model selection. Completed values are cached per Mac
process, and focus loss discards the target. Keyboard posting is not UI readback. The
Model Dial helper's Sync menu item reapplies the current panel values. Switching ChatGPT ↔
Cursor restores that app's last model and effort on the panel. When neither is
focused, encoder changes stay on the ESP32 display/NVS and the bridge discards
them without activating either app; turning the knob again while ChatGPT or
Cursor is focused is what applies a setting. Focus lost mid-apply discards the
change too. Model picker and confirmation waits use 0.25 s; all posted
keystrokes use a shared 0.05 s gap. A five-second hold on the glass starts
touch calibration.

```bash
chatgpt-bridge --watch --port "$ESP_PORT"
```

Requires Accessibility for the launching app (key posting and prompt focus).


## Locked ST7796 view mapping

Desk pose: glass left of breadboard, pins toward the ESP32, USB toward the
bottom of the frame. UI must read upright (focused app title at top of glass).
Locked settings in `main/display.c`: `invert_color(true)`, RGB, SPI 26 MHz;
`swap_xy(true)`, `mirror(true, true)` plus the internal-RAM soft-180 band
blit in `display_flush()` (title top-left, version bottom-right). This is
**not** the same as `hardware-test` `DISPLAY_PROFILE_ST7796U_3_5`
(`mirror(false, true)`). Never reverse the PSRAM framebuffer in place (races
SPI DMA, corrupts the title strip). Do not flip only one MADCTL mirror to
“fix” rotation (glyphs mirror). Do not remove the band blit without desk
verification. Keep repo `HARDWARE.md` in sync when this changes.

The header brand lockup is an 8-bit coverage mask in `main/logo.c`, generated
from `assets/` by `scripts/generate_logos.py` (needs Pillow) and committed.
Coverage is luminance × alpha so the Cursor cube keeps shaded faces. Both
logos share one `LOGO_HEIGHT` and baseline (identical header footprint).
Re-run the script and commit `main/logo.c` after artwork or height changes;
do not hand-edit the generated file.

## Encoder decode (PCNT vs polled)

Thinking uses a polled falling-CLK decode and holds pulses until the knob
pauses (two detents = one level; a quick turn can run Light↔Extra High). The
model knob uses PCNT hardware quadrature — a polled decode misreads it because
the display flush delays the poll past the CLK/DT phase difference and the dial
parks on one end. Thinking clamps Light ↔ Extra High or that Cursor model's
effort range.

## InputGuard and per-app apply

`InputGuard.swift` protects each complete model/effort apply with a
process-scoped active event tap. Keep bridge keys tagged in `Keys.swift`; user
presses, pointer movement, drags, and scrolling are discarded, never queued.
Release-side events pass through so input held before acquisition cannot become
stuck in the target app. Do not post keys if the filter cannot start. Preserve
serial supersession and release the filter on every exit. Escape, focus loss, disabled taps, and the
independent five-second watchdog cancel the target. The `--check-input-guard`
diagnostic acquires/releases without posting keys. A build or availability
check does not verify physical input suppression. See also
[input guard](mac-chatgpt-bridge/README.md#input-guard).

**Prompt focus.** Before posting shortcuts: Cursor Command-L only if Agents
is not already open (Cmd+L toggles the sidepanel and would close it);
otherwise AX-focus `aislash-editor-input`. ChatGPT/Codex by message-box
identity — never by screen coordinates.

**Per-app apply.**

- **ChatGPT:** Accessibility clicks the visible model control, then **Select model**
  and the exact ESP dial model label; then absolute reasoning (Ctrl+Shift+,
  clamp to Light, Ctrl+Shift-. up to target).
- **Cursor:** Command-/ (first Down is Auto); effort is Left, Up, Right into
  Reasoning after reopening (Right highlights the first supported level;
  Down to the target; Return; Escape twice closes menus). Model order is
  Auto then the enabled model list (no wrap). A ChatGPT-only model name
  (e.g. GPT-6 Astra) is skipped while Cursor is focused so effort can still
  apply. Keep `catalog.c` ↔ `Catalog.swift` aligned for per-model effort
  ranges (Unsupported / Max / None / Minimal extensions and clamp behavior).
  None is a selectable GPT effort, not a synonym for unsupported.
- **OpenCode:** fixed encoder order GPT-6 Astra, GPT-5.6 Terra, GPT-5.6 Sol,
  GPT-5.6 Luna. Command-apostrophe picker opens with Luna selected: Down
  3/2/1/0, then Return. Thinking stays local to the panel and is never
  synchronized. Keep firmware `opencode_models` and Swift `openCodeModels`
  aligned.

## Build / flash (Mac only)

After a directory rename, use a fresh build directory: ESP-IDF/CMake caches
absolute source paths. The Mac helper keeps its existing `mac-chatgpt-bridge`
folder and `chatgpt-bridge` command for compatibility.

Activate the local ESP-IDF environment and set `ESP_PORT` to the verified device.
From this project directory:

```sh
idf.py -B build build
idf.py -B build -p "$ESP_PORT" flash monitor
```

Verify the intended board and port before flashing. Use `--list-ports` on the
helper to list candidates. A successful build does not update the running board.

`VERSION` is the authoritative firmware version; update it intentionally for a
release. Builds generate `build_number.h` in their own build directory, then
archive the successfully generated binary under `<build-directory>/dist/`.
`CURRENT` is replaced only after the binary copies succeed. Source files and
version numbers are never modified by the build. Generated headers and archives
are not tracked in Git. Alternate `idf.py -B ...` directories are supported.

Two encoders on the **right** header (see repo `s3-n16r8.jpeg`): **thinking**
(GPIO41/40/39) and **model** (GPIO1/2/42). Rotate or click to step; the
panel updates immediately and SET waits 0.4 s after the last detent.

## Bridge watch

The Mac helper temporarily filters user input to the focused app during each
model/thinking apply. Escape cancels; focus loss or a five-second timeout releases
the filter and drops the target. See [input guard](mac-chatgpt-bridge/README.md#input-guard)
for permissions, held-input handling, and the availability check.

```sh
cd mac-chatgpt-bridge
swift build -c release
"$(swift build -c release --show-bin-path)/chatgpt-bridge" \
  --watch --port "$ESP_PORT"
```

## Code organization

- `main/main.c`: input/state coordination, 1 min clock screensaver, and save/paint retries.
- `main/ui.c`: drawing including the idle screensaver; `display.c`: SPI and DMA ownership; `canvas.c`/`font.c`: pixels/text.
- `main/front_title.c`: Mac `FRONT Cursor` / `FRONT ChatGPT` / `FRONT None`;
  None keeps the last app and does not restore ChatGPT.
- `main/logo.c`: header brand masks generated from `assets/` by
  `scripts/generate_logos.py`; re-run it (needs Pillow) after changing the
  artwork or its target height, and commit the result.
- `main/encoder.c`: existing GPIO/PCNT decoding and rate-limited step emission.
- `main/serial_model.c`: bounded framing and legacy display commands.
- `main/serial_sync.c`: state revisions, settling, snapshots, and ACK retries.
- `main/model_nvs.c`: persistence of the current pair, last model per app, the
  Cursor and ChatGPT enable masks, and per-app per-model effort;
  `model_parse.c`: legacy combined-name parsing.

Display rotation, the internal-RAM 180-degree band blit, and encoder pin/direction
configuration are unchanged. A DMA completion timeout retains buffer ownership;
the main loop retries rendering after 0.5 s instead of overwriting in-flight data.
The thinking encoder remains polled, so long display operations can still miss
physical edges; the pending-step fix does not replace the existing decoder.

### Cursor effort ranges

- Unsupported (knob ignored): Auto, Composer 2.5, Claude Opus 4.5, Claude Haiku 4.5, Claude Sonnet 4.5, Claude Sonnet 4, Gemini 3.1 Pro, Gemini 3 Flash, Gemini 3.5 Flash, GPT-5 Mini, Gemini 2.5 Flash, Kimi K2.7 Code.
- Low, Medium, High: Cursor Grok 4.5, Gemini 3.8 Flash, Gemini 3.7 Flash, GPT-5.1.
- Minimal, Low, Medium, High: Gemini 3.6 Flash.
- Low, Medium, High, Extra High: Cursor Grok 4.6, Codex 5.3, GPT-5.2.
- Low, Medium, High, Extra High, Max: Claude Opus 5 / 4.8 / 4.7, Claude Fable 5 / 5.1, Claude Sonnet 5.
- Low, Medium, High, Max: Claude Sonnet 4.6, Claude Opus 4.6.
- None, Low, Medium, High, Extra High, Max: GPT-5.6 Sol, Terra, Luna.
- None, Low, Medium, High, Extra High: GPT-5.5, GPT-5.4, GPT-5.4 Mini, GPT-5.4 Nano.
- Low, High, Max: Kimi K3.
- High, Max: GLM 5.2.

Command-/ → Left → Up → Right highlights the first supported effort.
Down moves by the target's zero-based index; Return selects, then Escape twice closes the menus. Model changes
reapply effort, even when the requested level is unchanged. Unsupported endpoints
clamp to the model's range. The bridge needs a known model for effort-only
commands; CLI callers supply `--set-model` together with `--set-thinking`.

## OpenCode

`FRONT OpenCode` selects an OpenCode wordmark header and its own fixed encoder:
GPT-6 Astra, GPT-5.6 Terra, GPT-5.6 Sol, then GPT-5.6 Luna. OpenCode's native
picker opens with Luna selected, so the bridge sends zero to three Down presses
followed by Return for the selected encoder entry. Its last model is stored
separately from ChatGPT and Cursor. Thinking remains local to the panel and does
not synchronize with OpenCode. `FRONT None` retains that state. An older saved
OpenCode model outside this set resets to GPT-5.6 Luna when OpenCode next becomes
focused.
See the [OpenCode bridge mapping](mac-chatgpt-bridge/README.md#opencode) for
model availability, effort mappings, and integration limits.

The event-driven recovery build retains version 0.90 by user request. Older
v0.90 binaries lack READY and require a serial reconnect after a silent reset
with this bridge. ENABLED is sent on SYNC or a model-list change; the
30-second TIME refresh remains for clock accuracy.
