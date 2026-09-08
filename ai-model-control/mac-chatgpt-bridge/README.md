# mac-chatgpt-bridge

macOS helper for the desk encoders. Foreground is one
`NSWorkspace.frontmostApplication` read. It never activates ChatGPT or Cursor.
While one of those apps is focused it focuses the prompt via Accessibility,
then posts that app's keyboard shortcuts; otherwise it drops the ESP32's
model and thinking state instead of holding it for later.

## What it does

1. Optional Accessibility check (needed to post keys and to focus the prompt).
2. Treats a desk target as foreground when the frontmost app is
   `com.openai.chat`, `com.openai.codex`, or Cursor
   (`com.todesktop.230313mzl4w4u92`). `--bundle-id` can force one of those.
3. Requests current state on connection and every two seconds. Accepts revisioned
   `STATE` updates and legacy `SET MODEL` / `SET THINKING` lines. ACKs validated
   updates on receipt; duplicate revisions are acknowledged without reapplying.
   An ACK means received, not applied.
4. If ChatGPT or Cursor is focused, focus the prompt first — Cursor via
   Command-L only when the Agents panel is missing (Cmd+L toggles it closed
   if it is already open), otherwise AX-focus `aislash-editor-input`;
   ChatGPT/Codex by message-box identity — then apply using that app's shortcuts.
   Applies start 1 s after the last
   received change. One worker retains the latest complete model/effort target,
   with a generation that changes on every accepted update or `PUSH` (SYNC tap).
   Serial is drained during delays; a newer generation aborts and re-targets,
   including when the dial returns to an earlier value. Model is applied before
   effort, and model changes invalidate the cached effort for both apps.
   Each field is marked unknown before posting keys, so an interrupted or failed
   sequence cannot cause a later correction to be skipped. Completed fields are
   cached per process only while their generation is current. `PUSH` forces both
   fields to be posted again.
   - ChatGPT / Codex model: Control-Shift-M (picker opens on Astra), Down
     to the ESP dial index, Return.
   - ChatGPT / Codex reasoning: absolute Light clamp (Ctrl+Shift+,) then
     climb with Ctrl+Shift-.
   - Cursor model: Command-/ opens Search; first Down is Auto, then the
     enabled picker order; Return selects. A ChatGPT-only name still on
     the panel (e.g. GPT-6 Astra) is skipped, not retried, so effort can
     still apply.
   - Cursor effort: Command-/ (reopened after selecting a model when
     both changed), then Left, Up, Right directly into Reasoning, Down to
     the level, and Return once, then Escape twice to close the menus. An effort-only change skips model selection.
5. If neither ChatGPT nor Cursor is focused: leave the ESP32 display/NVS as
   the source of truth and discard the change. Nothing is applied when one of
   those apps later becomes frontmost, and the helper never activates either
   app. The helper also sends `FRONT Cursor`, `FRONT ChatGPT`, or `FRONT None`
   so the panel lockup matches the focused app and can idle to a clock
   screensaver when neither is focused.

Shortcut sequences stay bound to the process that was focused when they began.
Losing focus, including switching between ChatGPT, Codex, and Cursor,
interrupts the sequence and discards the setting. A superseded sequence, or one
that failed while the app stayed focused, is retried for as long as that app
remains frontmost. Interrupted pickers are dismissed before retrying in that process, tracking
both Cursor menu layers and each Escape already posted. ChatGPT thinking retries
start from the absolute Light clamp. Logs say “posted” when the current generation's
key sequence completes; the helper does not read back the app's selected value.
Debounce reduces intermediate work, while generation checks and invalidation make
slow turns converge on the final target as long as the same app stays focused.
This is keyboard-posting completion, not verified on-screen synchronization.

Serial open, configuration, read, and write failures are logged and the configured
port is retried every two seconds, including when missing at startup. The helper
claims exclusive access to prevent another helper or monitor opening the port.
Current firmware retransmits until ACK and answers SYNC with its state, so a
bridge restart or device reset recovers the panel state without another knob
movement; whether it is applied still depends on ChatGPT or Cursor being
focused at that moment. Older firmware still works, but cannot replay missing
changes. If the device path changes, restart with the new `--port`.

See the [firmware protocol](../README.md#protocol-usb-serial-115200) for frame
formats and compatibility details. Buffers and bytes processed per poll are
bounded. Retry and key-delay durations use a monotonic clock.

Dial models follow the focused app.

ChatGPT: GPT-6 Astra, GPT-5.6 Sol, GPT-5.6 Terra, GPT-5.6 Luna, GPT-5.5.
Reasoning: Light, Medium, High, Extra High.

Cursor: Auto, then the enabled MODELS list (defaults: Cursor Grok 4.6,
Composer 2.5, Claude Opus 5, GPT-5.6 Sol, Claude Fable 5, GPT-5.6 Terra,
GPT-5.6 Luna). Effort depends on the model (see below).

Bind ChatGPT's three shortcuts if they are Unassigned. Cursor uses Command-/
to open the model list (first Down is Auto). Firmware waits 0.4 s after the
last encoder detent before sending a SET line.

## Requirements

- macOS 13+
- Swift toolchain (`xcode-select --install` or Xcode)
- ChatGPT and/or Cursor (keys fire only while one is already focused)
- Accessibility granted to the app that **launches** the CLI, for key posting
  and focusing the prompt field

## Accessibility grant

Foreground detection does not need Accessibility. Posting the ChatGPT and
Cursor shortcuts, and focusing the prompt field, do.

1. System Settings → Privacy & Security → Accessibility
2. Enable the terminal (or Cursor) you will run the CLI from
3. Confirm:

```sh
cd ai-model-control/mac-chatgpt-bridge
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
cd ai-model-control/mac-chatgpt-bridge
swift build -c release
"$(swift build -c release --show-bin-path)/chatgpt-bridge" \
  --watch --port "$ESP_PORT"
```

```sh
swift run chatgpt-bridge --hid-info
swift run chatgpt-bridge --list-ports
```

`Options.swift` owns CLI parsing; `BridgeRuntime.swift` owns the foreground and
apply loop; `SerialProtocol.swift` owns wire parsing; `Serial.swift`
owns serial I/O. `Apply.swift` keeps model and thinking shortcut sequences explicit
and returns typed applied/interrupted/failed outcomes.

Rebuilding does not replace a running helper. To deploy an updated binary, stop
the previous helper and relaunch it from the same Accessibility-authorized app.

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

The bridge recognizes the installed desktop app `ai.opencode.desktop` and sends
`FRONT OpenCode`. The panel shows an OpenCode wordmark header, uses the existing
GPT dial catalog, and saves its selections separately (`last_o` and app ID 2
in the existing effort blob). ChatGPT and Cursor keep their existing app IDs.

Command-apostrophe opens OpenCode's model picker. The bridge selects a unique
enabled Accessibility button by exact model name, so provider ordering does
not affect selection. The model must be available in OpenCode's picker;
missing or duplicate names fail without choosing another model.

Effort uses the accessible **Choose model variant** menu to select an absolute
value. Panel Light maps to Low, Medium to Medium, High to High, and Extra High
to Xhigh. The cycling Command-Shift-D shortcut is intentionally not used because
it cannot establish an absolute value without knowing the current selection.
Other OpenCode models, None/Max variants, and agent cycling are not exposed by
this initial GPT dial integration.

Prompt focus, interruption cleanup, per-process apply caching, SYNC reapply,
and dropping changes when the app is not foreground follow the existing bridge
workflow. A new firmware build is required for the OpenCode header and saved
state; rebuilding alone does not update the connected device or running helper.
