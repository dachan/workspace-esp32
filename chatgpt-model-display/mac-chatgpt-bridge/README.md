# mac-chatgpt-bridge

macOS helper for the desk encoders. It does **not** walk the Accessibility
tree. Foreground is one `NSWorkspace.frontmostApplication` read. While
ChatGPT or Cursor is focused it posts that app's keyboard shortcuts;
otherwise it queues the latest model and thinking state from the ESP32.

## What it does

1. Optional Accessibility check (needed only to post keys).
2. Treats a desk target as foreground when the frontmost app is
   `com.openai.chat`, `com.openai.codex`, or Cursor
   (`com.todesktop.230313mzl4w4u92`). `--bundle-id` can force one of those.
3. Requests current state on connection and every two seconds. Accepts revisioned
   `STATE` updates and legacy `SET MODEL` / `SET THINKING` lines. ACKs validated
   updates once queued; duplicate revisions are acknowledged without reapplying.
4. If ChatGPT or Cursor is focused (or just became focused with a queue),
   apply using that app's shortcuts. Applies start 1 s after the last
   received change so both knobs land in one pass, and a field equal to
   the last value applied to that app is skipped. Serial is drained during
   delays; a newer SET aborts and re-targets.
   - ChatGPT / Codex model: Control-Shift-M (picker opens on Astra), Down
     to the ESP dial index, Return.
   - ChatGPT / Codex reasoning: absolute Light clamp (Ctrl+Shift+,) then
     climb with Ctrl+Shift-.
   - Cursor model: Command-backslash opens Search; first Down is Auto, then the
     enabled picker order; Return selects. A ChatGPT-only name still on
     the panel (e.g. GPT-6 Astra) is skipped, not retried, so effort can
     still apply.
   - Cursor effort: Command-backslash (reopened after selecting a model when
     both changed), then Left, Up, Right directly into Reasoning, Down to
     the level, and Return once. An effort-only change skips model selection.
5. If neither ChatGPT nor Cursor is focused: leave the ESP32 display/NVS
   as the source of truth and apply the queued values when one of those
   apps becomes frontmost. The helper never activates either app. A
   `CANCEL` line from the panel drops the apply queue. The helper also sends
   `FRONT Cursor` or `FRONT ChatGPT` so the panel title matches the focused app.

Shortcut sequences stay bound to the process that was focused when they began.
Losing focus, including switching between ChatGPT, Codex, and Cursor,
interrupts the sequence and retains the setting for retry. An interrupted
model picker is dismissed with Escape before retrying in that process (an
extra 0.1 seconds). ChatGPT thinking retries start from the absolute Light
clamp. “Applied” means the key sequence was posted; the helper does not
read back the app's selected value.

Serial open, configuration, read, and write failures are logged and the configured
port is retried every two seconds, including when missing at startup. The helper
claims exclusive access to prevent another helper or monitor opening the port.
Queued settings survive reconnection. Current firmware retransmits until ACK and
answers SYNC with its state, so bridge restarts and device resets recover without
another knob movement. Older firmware still works, but cannot replay missing
changes. If the device path changes, restart with the new `--port`.

See the [firmware protocol](../README.md#protocol-usb-serial-115200) for frame
formats and compatibility details. Buffers and bytes processed per poll are
bounded. Retry and key-delay durations use a monotonic clock.

Dial models follow the focused app.

ChatGPT: GPT-6 Astra, GPT-5.6 Sol, GPT-5.6 Terra, GPT-5.6 Luna, GPT-5.5.
Reasoning: Light, Medium, High, Extra High.

Cursor: Auto, Cursor Grok 4.6, Composer 2.5, Claude Opus 5, GPT-5.6 Sol,
Claude Fable 5, GPT-5.6 Terra, GPT-5.6 Luna. Effort depends on the model (see below).

Bind ChatGPT's three shortcuts if they are Unassigned. Cursor uses Command-backslash
to open the model list (first Down is Auto). Firmware waits 0.4 s after the
last encoder detent before sending a SET line.

## Requirements

- macOS 13+
- Swift toolchain (`xcode-select --install` or Xcode)
- ChatGPT and/or Cursor (keys fire only while one is already focused)
- Accessibility granted to the app that **launches** the CLI, for key posting

## Accessibility grant

Foreground detection does not need Accessibility. Posting the ChatGPT and
Cursor shortcuts does.

1. System Settings → Privacy & Security → Accessibility
2. Enable the terminal (or Cursor) you will run the CLI from
3. Confirm:

```sh
cd chatgpt-model-display/mac-chatgpt-bridge
swift run chatgpt-bridge --check-ax
swift run chatgpt-bridge --front
```

## Desk control

```bash
chatgpt-bridge --watch --port "$ESP_PORT"
```

`--send-serial` is accepted for existing launch commands; it has no effect.
SYNC/ACK traffic is automatic. `--watch --port` is enough. Set `ESP_PORT` to the
verified device path. `--bundle-id` accepts `com.openai.chat`,
`com.openai.codex`, or `com.todesktop.230313mzl4w4u92`; unsupported baud
rates and setting names fail validation.

```sh
cd chatgpt-model-display/mac-chatgpt-bridge
swift build -c release
"$(swift build -c release --show-bin-path)/chatgpt-bridge" \
  --watch --port "$ESP_PORT"
```

```sh
swift run chatgpt-bridge --hid-info
swift run chatgpt-bridge --list-ports
```

`Options.swift` owns CLI parsing; `BridgeRuntime.swift` owns the pending queue and
foreground/apply loop; `SerialProtocol.swift` owns wire parsing; `Serial.swift`
owns serial I/O. `Apply.swift` keeps model and thinking shortcut sequences explicit
and returns typed applied/interrupted/failed outcomes.

Rebuilding does not replace a running helper. To deploy an updated binary, stop
the previous helper and relaunch it from the same Accessibility-authorized app.

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
