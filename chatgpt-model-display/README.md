# chatgpt-model-display

ESP32-S3 firmware that always shows the current ChatGPT/Codex **model** and
**thinking level** on the 2.8" ILI9341 panel used by `super-tamagotchi` /
`radar-receiver`.

## Protocol (USB serial, 115200)

UTF-8 lines, 115200. Directions are not interchangeable (avoids an echo loop):

```text
MODEL <name>           # Mac → ESP display
THINKING <level>       # Mac → ESP display
SET MODEL <name>       # ESP → Mac (encoder); firmware ignores on RX
SET THINKING <level>   # ESP → Mac (encoder); firmware ignores on RX
```

`<name>` is the Accessibility readback string or a preset from the model
encoder. Thinking level is often already embedded (for example
`GPT-5.6 Luna Extra High`). The firmware splits a trailing thinking token when
present; otherwise it shows the full string as the model and `—` for thinking.

On each `MODEL` line the firmware saves model/thinking to NVS (`cgpt`/`model`,`think`) and reloads that cache on boot so the panel is not stuck on Waiting when the bridge is quiet. After a local encoder `SET`, inbound `MODEL`/`THINKING` are ignored for 8s so a stale Mac poll cannot overwrite the knob.

Optional Mac override (until the next `MODEL` line):

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
| Thinking ENC CLK | 41 |
| Thinking ENC DT | 40 |
| Thinking ENC SW | 39 |
| Model ENC CLK | 1 |
| Model ENC DT | 2 |
| Model ENC SW | 42 |
| Both ENC + | 3V3 |
| Both ENC GND | GND |

USB: native USB Serial/JTAG (`/dev/cu.usbmodem*` on macOS). Flash,
`MODEL`/`THINKING`, and encoder `SET` traffic share that port.

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

Two encoders on the **right** header (see repo `s3-n16r8.jpeg`): **thinking** (GPIO41/40/39) and **model** (GPIO1/2/42). Rotate or click to step; changes persist in NVS and emit `SET …` to the Mac. Model rotate and click **clamp** at the first and last preset (no wrap). Thinking rotate clamps Instant…Extra High.

## Desk bridge (watch + port + listen)

```sh
cd ~/Development/workspace-esp32/mac-chatgpt-bridge
swift build -c release
"$(swift build -c release --show-bin-path)/chatgpt-bridge" \
  --watch --listen --send-serial --interval 5 \
  --port /dev/cu.usbmodem21201
```

`--watch --port` implies `--listen`. The helper applies encoder `SET` lines in
ChatGPT and sends `MODEL`/`THINKING` only when the AX readback changes and the
post-encoder hold has expired.
