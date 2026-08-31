#!/usr/bin/env python3
"""Interactive emulator for the creature.

Runs the real creature.c / gfx.c as a live subprocess and drives it from a
browser: the mouse is the touch panel, and every frame is produced by the same
code the firmware runs. Not a recording — poking it here does exactly what
poking the panel does.

Raw RGB565 goes over localhost rather than encoded images. A frame is 150KB,
which is nothing over loopback, and it skips a compression step per frame that
would otherwise cap the frame rate.

    python3 sim/serve.py        then open http://localhost:8765
"""

import http.server
import os
import subprocess
import sys
import threading
import time
import urllib.parse

WIDTH, HEIGHT = 320, 240
FRAME_BYTES = WIDTH * HEIGHT * 2
PORT = 8765

# Measured on the board: a full 150KB frame down a 40MHz SPI bus costs ~31ms, so
# the firmware is pinned at 31.3fps. The desktop will happily run at 60+, which
# makes the motion look smoother here than it ever will on the device. Pace to
# the hardware instead, so timing judged in the emulator holds on the panel.
BOARD_FPS = 31.3
FRAME_INTERVAL = 1.0 / BOARD_FPS

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BINARY = os.path.join(ROOT, "sim", "creature_live")

_lock = threading.Lock()
_proc = None
_next_frame = 0.0
_binary_mtime = 0.0


def start_creature():
    global _proc, _binary_mtime
    if not os.path.exists(BINARY):
        sys.exit(f"{BINARY} not found — run ./sim/run.sh first")
    if _proc is not None:
        _proc.terminate()
        _proc.wait()
    _binary_mtime = os.path.getmtime(BINARY)
    _proc = subprocess.Popen([BINARY], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, bufsize=0)


def reload_if_rebuilt():
    """Picks up a rebuild without needing the server restarted.

    The creature runs as a long-lived subprocess, so recompiling while the server
    is up leaves it happily serving the old code — which looks exactly like a
    change that did not work, and wastes a lot of time before anyone suspects the
    tooling instead of the edit.
    """
    try:
        mtime = os.path.getmtime(BINARY)
    except OSError:
        return
    if mtime != _binary_mtime:
        print("binary changed — reloading creature")
        start_creature()


def step(touched, x, y):
    """Advances the creature one frame and returns its framebuffer.

    Paced to the board's frame rate and given the board's timestep, so the
    browser's own refresh rate cannot influence how the animation looks.
    """
    global _next_frame
    with _lock:
        reload_if_rebuilt()
        now = time.monotonic()
        if now < _next_frame:
            time.sleep(_next_frame - now)
        _next_frame = max(_next_frame + FRAME_INTERVAL, time.monotonic())

        dt = FRAME_INTERVAL
        _proc.stdin.write(f"{dt:.5f} {int(touched)} {int(x)} {int(y)}\n".encode())
        _proc.stdin.flush()
        buf = bytearray()
        while len(buf) < FRAME_BYTES:
            chunk = _proc.stdout.read(FRAME_BYTES - len(buf))
            if not chunk:
                raise RuntimeError("creature process exited")
            buf.extend(chunk)
        return bytes(buf)


PAGE = """<!doctype html>
<meta charset="utf-8">
<title>super-tamagotchi — live</title>
<style>
  :root {{ color-scheme: dark; }}
  body {{ margin:0; min-height:100vh; display:flex; flex-direction:column;
         align-items:center; justify-content:center; gap:1rem;
         font:14px/1.5 ui-sans-serif,system-ui,sans-serif;
         background:#14121a; color:#cfc7d8; }}
  canvas {{ width:640px; max-width:92vw; height:auto; image-rendering:pixelated;
           border-radius:12px; box-shadow:0 12px 40px #0009; cursor:pointer;
           touch-action:none; }}
  .hint {{ color:#8d84a0; }}
  code {{ color:#a99cbd; }}
</style>
<canvas id="c" width="{w}" height="{h}"></canvas>
<div class="hint">click and drag on the creature — the mouse is the touch panel</div>
<div class="hint">paced to the board: {fps} fps, 8KB stack budget, touch jitter</div>
<div><span id="fps"></span></div>
<p class="hint"><code>creature.c</code> is running live; this is not a recording</p>
<script>
const W={w}, H={h};
const cv = document.getElementById('c'), cx = cv.getContext('2d');
const img = cx.createImageData(W, H);
const fpsEl = document.getElementById('fps');

let touched = false, tx = 0, ty = 0;
let frames = 0, fpsMark = performance.now(), inflight = false;

function pos(e) {{
  const r = cv.getBoundingClientRect();
  tx = Math.round((e.clientX - r.left) / r.width * W);
  ty = Math.round((e.clientY - r.top) / r.height * H);
}}
cv.addEventListener('pointerdown', e => {{ touched = true; pos(e); cv.setPointerCapture(e.pointerId); }});
cv.addEventListener('pointermove', e => {{ if (touched) pos(e); }});
cv.addEventListener('pointerup',   () => {{ touched = false; }});
cv.addEventListener('pointercancel',() => {{ touched = false; }});

async function loop() {{
  if (!inflight) {{
    inflight = true;
    const now = performance.now();
    try {{
      const r = await fetch(`/frame?t=${{touched?1:0}}&x=${{tx}}&y=${{ty}}`);
      const buf = new Uint8Array(await r.arrayBuffer());
      const px = img.data;
      for (let i = 0, p = 0; i < W * H; i++) {{
        // Framebuffer is byte-swapped RGB565, exactly as the ILI9341 takes it.
        const c = (buf[i*2] << 8) | buf[i*2+1];
        px[p++] = ((c >> 11) & 0x1F) << 3;
        px[p++] = ((c >> 5) & 0x3F) << 2;
        px[p++] = (c & 0x1F) << 3;
        px[p++] = 255;
      }}
      cx.putImageData(img, 0, 0);
      if (++frames >= 30) {{
        fpsEl.textContent = (30000 / (now - fpsMark)).toFixed(1) + ' fps';
        fpsMark = now; frames = 0;
      }}
    }} catch (err) {{ fpsEl.textContent = 'disconnected — ' + err.message; }}
    inflight = false;
  }}
  requestAnimationFrame(loop);
}}
requestAnimationFrame(loop);
</script>
"""


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass  # one line per frame is not useful

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        if url.path == "/":
            body = PAGE.format(w=WIDTH, h=HEIGHT, fps=BOARD_FPS).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if url.path == "/frame":
            q = urllib.parse.parse_qs(url.query)
            try:
                frame = step(q.get("t", ["0"])[0] == "1",
                             int(q.get("x", ["0"])[0]),
                             int(q.get("y", ["0"])[0]))
            except Exception as exc:
                self.send_error(500, str(exc))
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(frame)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(frame)
            return

        self.send_error(404)


def main():
    start_creature()
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"creature running live at http://localhost:{PORT}")
    print("mouse = touch panel.  ctrl-c to stop.")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        _proc.terminate()


if __name__ == "__main__":
    main()
