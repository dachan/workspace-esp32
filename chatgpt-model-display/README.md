# chatgpt-model-display

ESP32-S3 firmware that shows the ChatGPT **model** and **thinking** level
on the desk-mounted 3.5" ST7796U panel. Rotary encoders change both locally
(display + NVS). After **1 s** with no further detents the firmware sends
`SET MODEL` / `SET THINKING` to `mac-chatgpt-bridge/`. The Mac helper
applies those only while ChatGPT is already the foreground app.

## Protocol (USB serial, 115200)

Mac → ESP (optional display updates):

```text
MODEL <name>
THINKING <level>
```

ESP → Mac (after the 1 s encoder settle):

```text
SET MODEL <name>
SET THINKING <level>
```

On each encoder change the firmware saves model/thinking to NVS
(`cgpt`/`model`,`think`) and reloads that cache on boot.

Dial models, in order: GPT-6 Astra, GPT-5.6 Sol, GPT-5.6 Terra,
GPT-5.6 Luna, GPT-5.5, GPT-5.4 Mini.

Thinking: Light, Medium, High, Extra High.

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
`SET` traffic share that port.

## Desk control (encoders → ChatGPT)

Firmware `v 0.35+` updates the panel immediately, then sends SET after a
1 s rotary debounce. The Mac helper uses `NSWorkspace.frontmostApplication`
(no AX tree walk). While ChatGPT is focused it opens the model picker with
Control-Shift-M and steps reasoning with Control-Shift-, / Control-Shift-.
When ChatGPT is not focused, encoder changes stay on the ESP32 display/NVS
and the bridge queues the latest values without activating ChatGPT.

```bash
chatgpt-bridge --watch --send-serial --port /dev/cu.usbmodem21201
```

Requires Accessibility for the launching app (key posting only).

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

Two encoders on the **right** header (see repo `s3-n16r8.jpeg`): **thinking**
(GPIO41/40/39) and **model** (GPIO1/2/42). Rotate or click to step; the
panel updates immediately and SET waits 1 s after the last detent.

## Bridge watch

```sh
cd ~/Development/workspace-esp32/chatgpt-model-display/mac-chatgpt-bridge
swift build -c release
"$(swift build -c release --show-bin-path)/chatgpt-bridge" \
  --watch --port /dev/cu.usbmodem21201
```
