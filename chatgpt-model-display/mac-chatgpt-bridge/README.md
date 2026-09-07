# mac-chatgpt-bridge

macOS helper for the desk encoders. It does **not** walk ChatGPT's
Accessibility tree. Foreground is one `NSWorkspace.frontmostApplication`
read. While ChatGPT is focused it posts keyboard shortcuts; otherwise it
queues the latest model and thinking state from the ESP32.

## What it does

1. Optional Accessibility check (needed only to post keys).
2. Treats ChatGPT as foreground when the frontmost app is
   `com.openai.chat` or `com.openai.codex`.
3. Requests current state on connection and every two seconds. Accepts revisioned
   `STATE` updates and legacy `SET MODEL` / `SET THINKING` lines. ACKs validated
   updates once queued; duplicate revisions are acknowledged without reapplying.
4. If ChatGPT is focused (or just became focused with a queue):
   - Model: Control-Shift-M (picker opens on Astra), Down to the ESP
     dial index, Return. Serial is drained during delays; a newer SET
     aborts and re-targets.
   - Reasoning: absolute Light clamp (Ctrl+Shift+,) then climb with
     Ctrl+Shift-.
5. If ChatGPT is not focused: leave the ESP32 display/NVS as the source
   of truth and apply the queued values when ChatGPT becomes frontmost.
   The helper never activates ChatGPT. Cursor is never a target.

Shortcut sequences stay bound to the process that was focused when they began.
Losing focus, including switching between ChatGPT and Codex, interrupts the
sequence and retains the setting for retry. An interrupted model picker is
dismissed with Escape before retrying in that process (an extra 0.1 seconds).
Thinking retries start from the absolute Light clamp. “Applied” means the key
sequence was posted; the helper does not read back the app's selected value.

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

Dial models, in order:

- GPT-6 Astra
- GPT-5.6 Sol
- GPT-5.6 Terra
- GPT-5.6 Luna
- GPT-5.5

Reasoning: Light, Medium, High, Extra High.

Bind the three shortcuts in ChatGPT if they are Unassigned. Firmware waits
0.4 s after the last encoder detent before sending a SET line.

## Requirements

- macOS 13+
- Swift toolchain (`xcode-select --install` or Xcode)
- ChatGPT desktop app (keys fire only while it is already focused)
- Accessibility granted to the app that **launches** the CLI, for key posting

## Accessibility grant

Foreground detection does not need Accessibility. Posting Control-Shift-M
and the reasoning shortcuts does.

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
verified device path. `--bundle-id` only accepts `com.openai.chat` or
`com.openai.codex`; unsupported baud rates and setting names fail validation.

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
