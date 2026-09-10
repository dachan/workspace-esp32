# AI model control bridge (`mac-chatgpt-bridge`)

macOS helper for the AI model control panel's encoders. Foreground is one
`NSWorkspace.frontmostApplication` read. It never activates ChatGPT, Cursor, or OpenCode.
While one of those apps is focused it focuses the prompt via Accessibility,
then posts that app's keyboard shortcuts; otherwise it drops the ESP32's
model and thinking state instead of holding it for later.

## What it does

1. Optional Accessibility check (needed to post keys and to focus the prompt).
2. Treats a supported AI app as foreground when the frontmost app is
   ChatGPT/Codex (`com.openai.chat`, `com.openai.codex`), Cursor
   (`com.todesktop.230313mzl4w4u92`), or OpenCode (`ai.opencode.desktop`).
   `--bundle-id` can force one of those.
3. Requests current state on connection and each firmware READY announcement. Accepts revisioned
   `STATE` updates and legacy `SET MODEL` / `SET THINKING` lines. ACKs validated
   updates on receipt; duplicate revisions are acknowledged without reapplying.
   An ACK means received, not applied.
4. If ChatGPT, Cursor, or OpenCode is focused, focus the prompt first — Cursor via
   Command-L only when the Agents panel is missing (Cmd+L toggles it closed
   if it is already open), otherwise AX-focus `aislash-editor-input`;
   ChatGPT/Codex by message-box identity — then apply using that app's shortcuts.
   Applies start 0.25 s after the last
   received change. One worker retains the latest complete model/effort target,
   with a generation that changes on every accepted update or `PUSH` (SYNC tap).
   Serial is drained during delays; a newer generation aborts and re-targets,
   including when the dial returns to an earlier value. Model is applied before
   effort, and model changes invalidate the cached effort for both apps.
   Each field is marked unknown before posting keys, so an interrupted or failed
   sequence cannot cause a later correction to be skipped. Completed fields are
   cached per process only while their generation is current. `PUSH` forces both
   fields to be posted again. Model picker and confirmation waits use 0.25 s;
   every bridge-posted keystroke uses a shared 0.05 s gap.
   - ChatGPT / Codex model: click the visible model control by Accessibility,
     then **Select model** and the exact ESP dial model label.
   - ChatGPT / Codex reasoning: absolute Light clamp (Ctrl+Shift+,) then
     climb with Ctrl+Shift-.
   - Cursor model: Command-/ opens Search; first Down is Auto, then the
     enabled picker order; Return selects. A ChatGPT-only name still on
     the panel (e.g. GPT-6 Astra) is skipped, not retried, so effort can
     still apply.
   - Cursor effort: Command-/ (reopened after selecting a model when
     both changed), then Left, Up, Right directly into Reasoning, Down to
     the level, and Return once, then Escape twice to close the menus. An effort-only change skips model selection.
5. If none of ChatGPT, Cursor, or OpenCode is focused: leave the ESP32 display/NVS as
   the source of truth and discard the change. Nothing is applied when one of
   those apps later becomes frontmost, and the helper never activates either
   app. The helper also sends `FRONT Cursor`, `FRONT ChatGPT`, `FRONT OpenCode`, or `FRONT None`
   so the panel lockup matches the focused app and can idle to a clock
   screensaver when none is focused.

Shortcut sequences stay bound to the process that was focused when they began.
Losing focus, including switching between ChatGPT, Codex, Cursor, and OpenCode,
interrupts the sequence and discards the setting. A superseded sequence, or one
that failed while the app stayed focused, is retried for as long as that app
remains frontmost. Interrupted pickers are dismissed before retrying in that process, tracking
both Cursor menu layers and each Escape already posted. ChatGPT thinking retries
start from the absolute Light clamp. Logs say “posted” when the current generation's
key sequence completes; the helper does not read back the app's selected value.
Debounce reduces intermediate work, while generation checks and invalidation make
slow turns converge on the final target as long as the same app stays focused.
This is keyboard-posting completion, not verified on-screen synchronization.

Serial input is flushed on every open before `SYNC` requests fresh panel state, so
queued `PUSH` frames from a disabled bridge cannot replay after reconnecting.
Open, configuration, read, and write failures are logged and the configured
port is retried every two seconds, including when missing at startup. The helper
claims exclusive access to prevent another helper or monitor opening the port.
Current firmware retransmits until ACK and answers SYNC with its state, so a
bridge restart or device reset recovers the panel state without another knob
movement; whether it is applied still depends on ChatGPT, Cursor, or OpenCode being
focused at that moment. Older firmware still works, but cannot replay missing
changes. If the device path changes, restart with the new `--port`.

See the [firmware protocol](../README.md#protocol-usb-serial-115200) for frame
formats and compatibility details. Buffers and bytes processed per poll are
bounded. Retry and key-delay durations use a monotonic clock.

The Model Dial Settings window controls the enabled lists sent to the panel.
ChatGPT offers Light, Medium, High, Extra High, Max, and Ultra; at least Light
stays enabled. Cursor offers the full model catalog; Auto is always enabled.
Changes restart the bridge so the new masks are sent immediately. The CLI
accepts the same values with --chatgpt-effort-mask HEX and
--cursor-model-mask HEX; the bridge configuration is authoritative when it
receives the panel's informational ENABLED snapshot.

Dial models follow the focused app.

ChatGPT: GPT-6 Astra, GPT-5.6 Sol, GPT-5.6 Terra, GPT-5.6 Luna, GPT-5.5.
Reasoning: Light, Medium, High, Extra High, Max, Ultra (filtered by Settings).

Cursor: Auto, then the enabled model list (defaults: Cursor Grok 4.6,
Composer 2.5, Claude Opus 5, GPT-5.6 Sol, Claude Fable 5, GPT-5.6 Terra,
GPT-5.6 Luna). Effort depends on the model (see below).

Bind ChatGPT's three shortcuts if they are Unassigned. Cursor uses Command-/
to open the model list (first Down is Auto). Firmware waits 0.4 s after the
last encoder detent before sending a SET line.

## Input guard

Model and thinking changes share a temporary input filter for the focused app's
process, including the waits between menu steps. Keyboard, pointer, click, and
scroll events to that app are discarded while the bridge's tagged keys pass
through. Discarded input is not replayed. Other apps and system shortcuts are
not locked; leaving the target app interrupts the apply.

The bridge waits for held keys and mouse buttons to be released before starting.
Escape cancels the transaction. The filter is removed on every exit; an independent
five-second watchdog disables it even if an Accessibility call stalls. Focus loss,
Escape, or filter failure/timeout during an apply drops that target; use the dial
or SYNC to try again. A newer dial state can supersede the current sequence.
When creating the filter fails, the bridge posts no keys and reports a permission
error (check Accessibility and Input Monitoring for the launching app).

With a supported app focused, `chatgpt-bridge --check-input-guard` checks whether
the filter can be enabled and immediately releases it without posting keys.
This confirms availability, not physical input suppression or the app's selection.

## Requirements

- macOS 13+
- Swift toolchain (`xcode-select --install` or Xcode)
- ChatGPT, Cursor, and/or OpenCode (keys fire only while one is already focused)
- Accessibility granted to the app that **launches** the CLI, for key posting
  and focusing the prompt field

## Accessibility grant

Foreground detection and `FRONT` updates over serial do not need Accessibility.
Posting shortcuts and focusing the prompt field do; without it, the bridge keeps
sending the panel's focused-app state and drops keyboard-control requests.

1. System Settings → Privacy & Security → Accessibility
2. Enable the terminal (or Cursor) you will run the CLI from
3. Confirm:

```sh
cd ai-model-control/mac-chatgpt-bridge
swift run chatgpt-bridge --check-ax
swift run chatgpt-bridge --front
```

## AI model control

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

## Model Dial app

\`model-dial\` is the macOS menu-bar wrapper for this bridge. It starts and
supervises \`chatgpt-bridge\`, finds a supported USB serial path, reconnects when
the device path changes, and can register itself to start at login. The bridge
still reconnects independently when a device temporarily disappears at the same
path.

Build an unsigned local app bundle, optionally with the firmware app binary:

\`\`\`sh
cd ai-model-control/mac-chatgpt-bridge
./scripts/package-app.sh --firmware ../build-v0.90-event-sync/ai-model-control.bin
open "dist/Model Dial.app"
\`\`\`

The app needs Accessibility permission to post app shortcuts. Its **Open at
login** control registers the app with macOS. The app must run in the logged-in
desktop session; a system daemon cannot inspect or control the foreground app.
The menu's **Settings…** item opens the enabled ChatGPT effort and Cursor model
lists. The **Open Log** item runs \`tail -F\` on the persistent log at
\`~/Library/Logs/Model Dial/bridge.log\`.

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
`FRONT OpenCode`. The panel shows an OpenCode wordmark header and its encoder
order: GPT-6 Astra, GPT-5.6 Terra, GPT-5.6 Sol, then GPT-5.6 Luna. Firmware and
bridge validate OpenCode against that list, then save its selections separately
(`last_o` and app ID 2 in the existing effort blob). ChatGPT and Cursor keep
their existing app IDs.

Command-apostrophe opens OpenCode's Luna-first model picker with Luna already
highlighted. The bridge sends zero-based Down presses: 0 for Luna, 1 for Sol,
2 for Terra, or 3 for Astra, then Return to confirm the selection. The native
picker must retain that order.

OpenCode effort sync is unsupported. The panel may retain its local thinking
value, but the bridge skips OpenCode thinking updates; it does not open the
variant menu or use the cycling shortcut.

Prompt focus, interruption cleanup, per-process apply caching, SYNC reapply,
and dropping changes when the app is not foreground follow the existing bridge
workflow. A new firmware build is required for the OpenCode header and saved
state; rebuilding alone does not update the connected device or running helper.

The event-driven firmware build (still version 0.90) retries
`READY <16-hex boot id>` until SYNC. This replaces periodic state polling,
including after silent USB-preserving resets. Earlier firmware binaries need
a serial reconnect after such a reset. TIME still refreshes every 30 seconds;
ENABLED arrives on SYNC or a model-list change.
