#!/usr/bin/env python3
"""Turns the simulator's frame grid into a self-contained looping animation.

Encodes the BMP the C renderer produced as a PNG (zlib is in the standard
library, so nothing needs installing), embeds it as a data URI, and writes an
HTML page that steps through the grid on a canvas. Flat cartoon fills compress
extremely well, so the whole animation lands in a few hundred KB.
"""

import base64
import struct
import sys
import zlib


def read_bmp(path):
    with open(path, "rb") as f:
        data = f.read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    top_down = height < 0
    height = abs(height)
    row_bytes = width * 3
    pad = (4 - (row_bytes % 4)) % 4

    rows = []
    for y in range(height):
        start = offset + y * (row_bytes + pad)
        bgr = data[start:start + row_bytes]
        # BMP stores BGR; PNG wants RGB.
        rows.append(bytes(bgr[i + 2 - (i % 3) * 2] if False else 0 for i in range(0)) or
                    bytes(b for px in range(width)
                          for b in (bgr[px * 3 + 2], bgr[px * 3 + 1], bgr[px * 3 + 0])))
    if not top_down:
        rows.reverse()
    return width, height, rows


def encode_png(width, height, rows):
    raw = b"".join(b"\x00" + row for row in rows)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) +
            chunk(b"IEND", b""))


PAGE = """<!doctype html>
<meta charset="utf-8">
<title>super-tamagotchi — creature preview</title>
<style>
  :root {{ color-scheme: light dark; }}
  body {{ margin:0; min-height:100vh; display:flex; flex-direction:column;
         align-items:center; justify-content:center; gap:1rem;
         font:14px/1.5 ui-sans-serif,system-ui,sans-serif;
         background:#14121a; color:#cfc7d8; }}
  canvas {{ width:640px; max-width:92vw; height:auto; image-rendering:pixelated;
           border-radius:12px; box-shadow:0 12px 40px #0009; background:#f6f4f8; }}
  .row {{ display:flex; gap:.75rem; align-items:center; }}
  button {{ font:inherit; padding:.4rem .9rem; border-radius:999px;
           border:1px solid #4a4358; background:#221e2c; color:inherit;
           cursor:pointer; }}
  button:hover {{ background:#2c2738; }}
  code {{ color:#a99cbd; }}
</style>
<canvas id="c" width="{w}" height="{h}"></canvas>
<div class="row">
  <button id="play">Pause</button>
  <button id="step">Step</button>
  <span id="info"></span>
</div>
<p><code>./sim/run.sh</code> regenerates this from creature.c</p>
<script>
const COLS={cols}, ROWS={rows}, FRAMES={frames}, FPS={fps};
const FW={w}, FH={h};
const img = new Image();
img.src = "data:image/png;base64,{b64}";
const cv = document.getElementById('c'), cx = cv.getContext('2d');
const info = document.getElementById('info');
let i = 0, playing = true, last = 0;

function draw() {{
  const col = i % COLS, row = (i / COLS) | 0;
  cx.clearRect(0, 0, FW, FH);
  cx.drawImage(img, col * FW, row * FH, FW, FH, 0, 0, FW, FH);
  info.textContent = `frame ${{i + 1}}/${{FRAMES}} · ${{FPS}}fps · ${{(FRAMES / FPS).toFixed(1)}}s loop`;
}}
function tick(ts) {{
  if (playing && ts - last >= 1000 / FPS) {{ last = ts; i = (i + 1) % FRAMES; draw(); }}
  requestAnimationFrame(tick);
}}
img.onload = () => {{ draw(); requestAnimationFrame(tick); }};
document.getElementById('play').onclick = e => {{
  playing = !playing; e.target.textContent = playing ? 'Pause' : 'Play';
}};
document.getElementById('step').onclick = () => {{
  playing = false; document.getElementById('play').textContent = 'Play';
  i = (i + 1) % FRAMES; draw();
}};
</script>
"""


def main():
    cols, rows, frames, fps = (int(v) for v in sys.argv[1:5])
    width, height, bmp_rows = read_bmp("sim/film.bmp")
    png = encode_png(width, height, bmp_rows)

    with open("sim/creature.html", "w") as f:
        f.write(PAGE.format(
            cols=cols, rows=rows, frames=frames, fps=fps,
            w=width // cols, h=height // rows,
            b64=base64.b64encode(png).decode(),
        ))
    print(f"wrote sim/creature.html ({len(png) // 1024} KB animation, "
          f"{frames} frames)")


if __name__ == "__main__":
    main()
