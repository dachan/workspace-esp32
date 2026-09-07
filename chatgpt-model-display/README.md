# chatgpt-model-display

ESP32-S3 firmware that shows the ChatGPT **model** and **thinking** level
on the desk-mounted 3.5" ST7796U panel. Rotary encoders change both locally
(display + NVS). After **0.4 s** with no further changes the firmware sends
the latest state to `mac-chatgpt-bridge/`. The Mac helper
applies those only while ChatGPT or Cursor is already the foreground app.

## Protocol (USB serial, 115200)

The current helper sends `SYNC` on connection and every two seconds. Firmware
responds with its latest model and thinking after any active 0.4 s settle window:

```text
Mac → ESP: SYNC
ESP → Mac: STATE <16-hex revision> MODEL <name>
ESP → Mac: STATE <16-hex revision> THINKING <level>
Mac → ESP: ACK <16-hex revision> MODEL
Mac → ESP: ACK <16-hex revision> THINKING
```

Each changed field gets a new revision, including after firmware restart.
Unacknowledged state retries every 0.5 s; a full USB transmit buffer retries after
0.2 s without blocking encoder polling. The helper acknowledges validated state
when it is queued and ignores repeated revisions for application purposes.
Acknowledgement does **not** confirm the app's selected value: keyboard posting
has no UI readback. Periodic SYNC also recovers a device reset without requiring
the USB device path to disappear.

Mac also sends `TIME <unix-seconds> <tz-offset-minutes>` on connect and every
30 s so the panel can show a local clock. Firmware ticks minutes from that
snapshot; it does not use Wi-Fi or SNTP. The helper also sends `FRONT Cursor`
or `FRONT ChatGPT` when the focused desk app changes so the top-left title
matches; it falls back to ChatGPT when neither is focused.

When neither ChatGPT nor Cursor is focused and dial state is waiting to apply,
the helper sends `QUEUED`; it sends `CLEAR` once the queue is empty or one of
those apps is focused.
The panel shows a **CANCEL** button at the bottom-left; tap it (or click
either encoder) to send `CANCEL`, drop the apply queue, and restore the
panel/NVS to the last known model and thinking. A five-second press-and-hold
anywhere on the glass starts a five-point touch calibration.

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

Changed values are saved once per input pass to NVS (`cgpt`/`model`,`think`)
and reloaded on boot. Unchanged values do not trigger persistence or display work.

Dial models follow the focused app. ChatGPT: GPT-6 Astra, GPT-5.6 Sol,
GPT-5.6 Terra, GPT-5.6 Luna, GPT-5.5; thinking Light, Medium, High,
Extra High. Cursor: Auto, Cursor Grok 4.6, Composer 2.5, Claude Opus 5,
GPT-5.6 Sol, Claude Fable 5, GPT-5.6 Terra, GPT-5.6 Luna;
effort depends on the model (see Cursor effort ranges below).
Canonical names live in firmware `main/catalog.c` and Swift
`Sources/Catalog.swift`; keep these small tables aligned when adding entries.

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

## Desk control (encoders → ChatGPT / Cursor)

Firmware `v 0.35+` updates the panel immediately, then sends SET after a
0.4 s settle window. Thinking pulses are held until the knob pauses so
two detents are one level and a quick turn can run Light↔Extra High. Model
steps still use a 160 ms emit gap. The Mac helper uses `NSWorkspace.frontmostApplication`
(no AX tree walk). While ChatGPT is focused it opens the model picker with
Control-Shift-M and steps reasoning with Control-Shift-, / Control-Shift-.
While Cursor is focused it opens the model list with Command-backslash (first Down
is Auto), then reopens it for Effort with Left, Up, Right, then Down-only to
the level; Return selects. The bridge
settles 1 s after the last received change, applies model and effort in one
pass, and skips any field that matches what it last applied to that app — an
effort-only change skips model selection. When neither is
focused, encoder changes stay on the ESP32 display/NVS and the bridge queues
the latest values without activating either app. A CANCEL button appears at
the bottom-left; tap it to drop that apply queue and restore the last known
model and thinking. A five-second hold on the glass starts touch calibration.

```bash
chatgpt-bridge --watch --port "$ESP_PORT"
```

Requires Accessibility for the launching app (key posting only).

## Build / flash (Mac only)

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

```sh
cd mac-chatgpt-bridge
swift build -c release
"$(swift build -c release --show-bin-path)/chatgpt-bridge" \
  --watch --port "$ESP_PORT"
```

## Code organization

- `main/main.c`: input/state coordination and save/paint retries.
- `main/ui.c`: drawing; `display.c`: SPI and DMA ownership; `canvas.c`/`font.c`: pixels/text.
- `main/front_title.c`: Mac `FRONT Cursor` / `FRONT ChatGPT` header text.
- `main/encoder.c`: existing GPIO/PCNT decoding and rate-limited step emission.
- `main/serial_model.c`: bounded framing and legacy display commands.
- `main/serial_sync.c`: state revisions, settling, snapshots, and ACK retries.
- `main/model_nvs.c`: persistence; `model_parse.c`: legacy combined-name parsing.

Display rotation, the internal-RAM 180-degree band blit, and encoder pin/direction
configuration are unchanged. A DMA completion timeout retains buffer ownership;
the main loop retries rendering after 0.5 s instead of overwriting in-flight data.
The thinking encoder remains polled, so long display operations can still miss
physical edges; the pending-step fix does not replace the existing decoder.

### Cursor effort ranges

- Auto, Composer 2.5: unsupported (effort knob ignored).
- Cursor Grok 4.6: Low, Medium, High, Extra High.
- Claude Opus 5, Claude Fable 5: Low, Medium, High, Extra High, Max.
- GPT-5.6 Sol, Terra, Luna: None, Low, Medium, High, Extra High, Max.

Command-backslash → Left → Up → Right highlights the first supported effort.
Down moves by the target's zero-based index; Return selects once. Model changes
reapply effort, even when the requested level is unchanged. Unsupported endpoints
clamp to the model's range. The bridge needs a known model for effort-only
commands; CLI callers supply `--set-model` together with `--set-thinking`.
