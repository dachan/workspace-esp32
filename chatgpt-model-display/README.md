# chatgpt-model-display

ESP32-S3 firmware that always shows the current ChatGPT/Codex **model** and
**thinking level** on the 2.8" ILI9341 panel used by `super-tamagotchi` /
`radar-receiver`.

## Protocol (USB serial, 115200)

UTF-8 lines from `mac-chatgpt-bridge`:

```text
MODEL <name>
```

`<name>` is the Accessibility readback string. Thinking level is often already
embedded (for example `GPT-5.6 Luna Extra High`). The firmware splits a trailing
thinking token when present; otherwise it shows the full string as the model and
`—` for thinking.

Optional extension (overrides parsed thinking until the next `MODEL` line):

```text
THINKING <level>
```

## Hardware

Same SPI ILI9341V pinout as `super-tamagotchi/WIRING.md` / radar-receiver:

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

USB: native USB Serial/JTAG (`/dev/cu.usbmodem*` on macOS). Flash and
`MODEL` traffic share that port.

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
`swift run --package-path ../mac-chatgpt-bridge chatgpt-bridge --list-ports`
before flashing (paths can change).

## Bridge watch → screen

```sh
cd ~/Development/workspace-esp32/mac-chatgpt-bridge
swift build -c release
"$(swift build -c release --show-bin-path)/chatgpt-bridge" \
  --watch --interval 5 --send-serial --port /dev/cu.usbmodem21201
```

Watch mode sends `MODEL <name>` only when the model string changes.
