# mac-chatgpt-bridge

macOS helper that reads the **currently selected model** from the ChatGPT
desktop app via Accessibility APIs. It can send that name to an ESP32-S3 over USB serial (`MODEL <name>
` at
115200). Pair with firmware in `../chatgpt-model-display/`.

## What it does

1. Checks Accessibility permission.
2. Finds a running ChatGPT app (`com.openai.chat` or the unified
   `com.openai.codex` bundle, also matched by process name).
3. Walks the AX tree and prints the selected model.
4. Optional `--dump-ax` dump if the model control is not obvious.

Serial path:

- USB serial line: `MODEL <name>\n` at 115200 (dry-run when `--port` is omitted;
  real open/write on macOS when `--port` is set)
- `--watch` polls and sends only when the model string changes
- Last successful model is cached at `~/Library/Application Support/chatgpt-bridge/last-model.txt`; on AX failure the bridge prints `using cached model: …` and still `--send-serial`s that line when requested
- Optional device-side `THINKING <level>\n` is documented in
  `chatgpt-model-display/README.md` (bridge does not emit it yet; thinking is
  usually already in the model name)
- Later ESP32 USB-HID model picker: **Ctrl+Shift+M** (documented, not sent)

## Requirements

- macOS 13+
- Swift toolchain (`xcode-select --install` or Xcode)
- ChatGPT desktop app running and signed in
- Accessibility granted to the app that **launches** the CLI

## Accessibility grant

The process that needs permission is usually Terminal, iTerm, or Cursor,
not the `chatgpt-bridge` binary itself.

1. System Settings → Privacy & Security → Accessibility
2. Enable the terminal (or Cursor) you will run the CLI from
3. Quit and reopen that app if it was already running
4. Confirm:

```sh
cd mac-chatgpt-bridge
swift run chatgpt-bridge --check-ax
```

Expected: `accessibility: granted`

If macOS never prompts, add the terminal app with `+` and toggle it off/on.

## Desk control (encoders → ChatGPT)

Firmware `v 0.23+` sends `SET MODEL <name>` / `SET THINKING <level>` when knobs change.
Run the Mac bridge with serial listen + watch:

```bash
chatgpt-bridge --watch --send-serial --port /dev/cu.usbmodem21201
```

Requires Accessibility (and Input Monitoring) for the launching Terminal.
Model knob clamps GPT-5.6 Luna ↔ o4-mini; thinking clamps Instant ↔ Extra High.

## Build and run


The current ChatGPT/Codex UI nests the model `AXPopUpButton` deep in the web tree; defaults use `--max-depth 32` / `--max-nodes 8000`.

```sh
cd mac-chatgpt-bridge
swift build -c release
"$(swift build -c release --show-bin-path)/chatgpt-bridge"
```

Or:

```sh
swift run chatgpt-bridge
```

Open ChatGPT, pick a model, then run the CLI. Do not hide the main window.

### Verify readback

1. In ChatGPT, select a distinctive model (for example `Thinking` or
   `GPT-5.2 Instant`).
2. Run:

```sh
swift run chatgpt-bridge
```

Expected stdout:

```text
ChatGPT model: Thinking
source: toolbar-picker AXPopUpButton+top
app: ChatGPT (com.openai.chat, pid 12345)
```

Exact source text depends on the AX path. The model line must match the
UI.

3. Switch models in ChatGPT and run the command again. The printed name
   should change.
4. JSON line:

```sh
swift run chatgpt-bridge --json
```

```json
{"appName":"ChatGPT","bundleID":"com.openai.chat","candidates":[{"model":"Thinking","path":"win0.0.1","role":"AXPopUpButton","score":10,"source":"toolbar-picker AXPopUpButton+top"}],"model":"Thinking","ok":true,"pid":12345,"source":"toolbar-picker AXPopUpButton+top"}
```

If readback fails, dump the tree and the scored hits:

```sh
swift run chatgpt-bridge --list-candidates
swift run chatgpt-bridge --dump-ax | less
```

`--dump-ax` is the debug path when the model control is not obvious.
`--bundle-id com.openai.chat` or `--bundle-id com.openai.codex` forces
one app if both Classic and unified ChatGPT are installed.



### Watch mode

Poll every 5 seconds and print only when the selected model changes:

```sh
swift run chatgpt-bridge --watch
swift run chatgpt-bridge --watch --interval 5
```

Optional serial push on change (dry-run without `--port`):

```sh
swift run chatgpt-bridge --watch --send-serial
swift run chatgpt-bridge --watch --send-serial --port /dev/cu.usbmodemXXXX
```

Stop with Ctrl+C. Combine with `--json` for one JSON object per change.

## Serial stub

Protocol (one line, UTF-8, newline terminated):

```text
MODEL <name>
```

No port → dry-run (prints the line, does not open a device):

```sh
swift run chatgpt-bridge --send-serial
```

With a device later:

```sh
swift run chatgpt-bridge --list-ports
swift run chatgpt-bridge --send-serial --port /dev/cu.usbmodemXXXX --baud 115200
```

Default baud is 115200. This repo does not flash firmware.

## HID stub

Later firmware: the ESP32-S3 enumerates as a USB keyboard and sends
**Ctrl+Shift+M** to open ChatGPT's model picker. This helper never
injects key events.

```sh
swift run chatgpt-bridge --hid-info
```

Confirm on hardware whether ChatGPT honors Control-Shift-M or
Command-Shift-M; firmware can remap.

## Notes

- AX labels move between ChatGPT builds. Prefer `--dump-ax` over guessing.
- Grant Accessibility to the launcher, then re-run. Unsigned CLI binaries
  inherit the terminal's TCC identity.
- Do not commit device paths, NVS dumps, or serial logs that identify
  hardware.
