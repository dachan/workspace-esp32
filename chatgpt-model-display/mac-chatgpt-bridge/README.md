# mac-chatgpt-bridge

macOS helper for the desk encoders. It does **not** walk ChatGPT's
Accessibility tree. Foreground is one `NSWorkspace.frontmostApplication`
read. While ChatGPT is focused it posts keyboard shortcuts; otherwise it
queues the latest `SET MODEL` / `SET THINKING` line from the ESP32.

## What it does

1. Optional Accessibility check (needed only to post keys).
2. Treats ChatGPT as foreground when the frontmost app is
   `com.openai.chat` or `com.openai.codex`.
3. Reads `SET MODEL <name>` / `SET THINKING <level>` from USB serial.
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

Serial read failures are logged and the configured port is retried every two
seconds. Queued settings survive reconnection, but changes sent while the port
was disconnected are not replayed by the firmware. If the device path changes,
restart with the new `--port`.

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
chatgpt-bridge --watch --send-serial --port /dev/cu.usbmodem21201
```

`--send-serial` is accepted so the old command still runs; the rewrite does
not push AX readback to the ESP32. `--watch --port` is enough.

```sh
cd chatgpt-model-display/mac-chatgpt-bridge
swift build -c release
"$(swift build -c release --show-bin-path)/chatgpt-bridge" \
  --watch --port /dev/cu.usbmodem21201
```

```sh
swift run chatgpt-bridge --hid-info
swift run chatgpt-bridge --list-ports
```
