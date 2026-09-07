# chatgpt-model-display

ESP32-S3 firmware that always shows the current ChatGPT/Codex **model** and
**thinking level** on the desk-mounted 3.5" ST7796U panel.

## Protocol (USB serial, 115200)

UTF-8 lines from `mac-chatgpt-bridge/` (in this folder):

```text
MODEL <name>
```

`<name>` is the Accessibility readback string. Thinking level is often already
embedded (for example `GPT-5.6 Luna Light`). The firmware splits a trailing
thinking token when present; otherwise it shows the full string as the model and
`—` for thinking.

On each `MODEL` line the firmware saves model/thinking to NVS (`cgpt`/`model`,`think`) and reloads that cache on boot so the panel is not stuck on Waiting when the bridge is quiet.

The bridge also sends the current thinking level separately:

```text
THINKING <level>
```

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
| Thinking ENC CLK | 41 |
| Thinking ENC DT | 40 |
| Thinking ENC SW | 39 |
| Model ENC CLK | 1 |
| Model ENC DT | 2 |
| Model ENC SW | 42 |
| Both ENC + | 3V3 |
| Both ENC GND | GND |

USB: native USB Serial/JTAG (`/dev/cu.usbmodem*` on macOS). Flash and
`MODEL` traffic share that port.

## Desk control (encoders → ChatGPT)

Firmware `v 0.23+` sends `SET MODEL <name>` / `SET THINKING <level>` when knobs change.
The bridge presses the app's Accessibility model/thinking controls directly and
falls back to configured keyboard shortcuts only when a control is unavailable.
Run the Mac bridge with serial listen + watch:

```bash
chatgpt-bridge --watch --send-serial --port /dev/cu.usbmodem21201
```

Requires Accessibility for the launching app. Input Monitoring is needed only
when the keyboard-shortcut fallback is used.
Model knob clamps to the current ChatGPT picker range (GPT-6 Astra through
GPT-5.4 Mini); thinking clamps Light ↔ Extra High.
When ChatGPT is not focused, encoder changes stay on the ESP32 display/NVS and
the bridge queues them without activating ChatGPT. It applies the latest queued
model and thinking level after ChatGPT returns to the foreground.

## Build / flash (Mac only)

```sh
cd ~/Development/workspace-esp32
git pull
source ~/esp/esp-idf-v6.0.2/export.sh
cd chatgpt-model-display
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem21201 flash monitor
```

Verify the port with `ls /dev/cu.usb*` or
`swift run --package-path ./mac-chatgpt-bridge chatgpt-bridge --list-ports`
before flashing (paths can change).

Two encoders on the **right** header (see repo `s3-n16r8.jpeg`): **thinking** (GPIO41/40/39) and **model** (GPIO1/2/42). Rotate or click to step; changes persist in NVS.

## Bridge watch → screen

```sh
cd ~/Development/workspace-esp32/chatgpt-model-display/mac-chatgpt-bridge
swift build -c release
"$(swift build -c release --show-bin-path)/chatgpt-bridge" \
  --watch --interval 5 --send-serial --port /dev/cu.usbmodem21201
```

Watch mode sends `MODEL <name>` only when the model string changes.
