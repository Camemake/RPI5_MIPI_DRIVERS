"""Live MJPEG view of the OV5640 on CAM0.

Reads UYVY from livecap and serves JPEG at http://0.0.0.0:8090/.
"""

from __future__ import annotations

import io
import os
import re
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import numpy as np
from PIL import Image

WIDTH = 640
HEIGHT = 480
FRAME = WIDTH * HEIGHT * 2
PORT = 8090

latest = b""
lock = threading.Lock()
fps = 0.0


def to_jpeg(packed: bytes) -> bytes:
    raw = np.frombuffer(packed[:FRAME], dtype=np.uint8).reshape(HEIGHT, WIDTH // 2, 4)
    u = raw[:, :, 0].astype(np.int32)
    y0 = raw[:, :, 1].astype(np.int32)
    v = raw[:, :, 2].astype(np.int32)
    y1 = raw[:, :, 3].astype(np.int32)
    y = np.empty((HEIGHT, WIDTH), dtype=np.int32)
    y[:, 0::2] = y0
    y[:, 1::2] = y1
    u = np.repeat(u, 2, axis=1)
    v = np.repeat(v, 2, axis=1)
    c = y - 16
    d = u - 128
    e = v - 128
    r = np.clip((298 * c + 409 * e + 128) >> 8, 0, 255).astype(np.uint8)
    g = np.clip((298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255).astype(np.uint8)
    b = np.clip((298 * c + 516 * d + 128) >> 8, 0, 255).astype(np.uint8)
    image = Image.fromarray(np.dstack((r, g, b)), "RGB")
    buf = io.BytesIO()
    image.save(buf, format="JPEG", quality=70)
    return buf.getvalue()


def prepare() -> None:
    media = "/dev/media0"
    text = ""
    for index in range(8):
        candidate = f"/dev/media{index}"
        found = subprocess.run(
            ["media-ctl", "-d", candidate, "-p"],
            capture_output=True,
            text=True,
        )
        if "ov5640" in found.stdout:
            media = candidate
            text = found.stdout
            break
    match = re.search(r"ov5640 [0-9a-f]+-[0-9a-f]+", text)
    sensor = match.group(0) if match else "ov5640 10-003c"
    fmt = "UYVY8_1X16/640x480 field:none"
    commands = [
        ["media-ctl", "-d", media, "-V", f"'{sensor}':0 [fmt:{fmt}]"],
        ["media-ctl", "-d", media, "-V", f"'csi2':0 [fmt:{fmt}]"],
        ["media-ctl", "-d", media, "-V", f"'csi2':4 [fmt:{fmt}]"],
        [
            "media-ctl",
            "-d",
            media,
            "-l",
            "'csi2':4 -> 'rp1-cfe-csi2_ch0':0 [1]",
        ],
    ]
    for command in commands:
        subprocess.run(command, check=True)


def capture() -> None:
    global latest, fps
    prepare()
    proc = subprocess.Popen(
        [os.path.join(os.path.dirname(os.path.abspath(__file__)), "livecap")],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert proc.stdout is not None
    frames = 0
    mark = time.monotonic()
    while True:
        packed = bytearray()
        while len(packed) < FRAME:
            chunk = proc.stdout.read(FRAME - len(packed))
            if not chunk:
                err = proc.stderr.read().decode(errors="replace") if proc.stderr else ""
                raise SystemExit(f"livecap ended: {err}")
            packed.extend(chunk)
        jpeg = to_jpeg(bytes(packed))
        with lock:
            latest = jpeg
        frames += 1
        now = time.monotonic()
        if now - mark >= 2:
            fps = frames / (now - mark)
            frames = 0
            mark = now
            print(f"{fps:.1f} fps", flush=True)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self) -> None:
        if self.path.startswith("/stream"):
            self.send_response(200)
            self.send_header("Cache-Control", "no-cache")
            self.send_header(
                "Content-Type", "multipart/x-mixed-replace; boundary=frame"
            )
            self.end_headers()
            try:
                while True:
                    with lock:
                        jpeg = latest
                    if not jpeg:
                        time.sleep(0.05)
                        continue
                    self.wfile.write(
                        b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "
                        + str(len(jpeg)).encode()
                        + b"\r\n\r\n"
                        + jpeg
                        + b"\r\n"
                    )
                    self.wfile.flush()
                    time.sleep(0.03)
            except (BrokenPipeError, ConnectionResetError):
                return
            return
        page = """<!doctype html>
<meta charset="utf-8">
<title>OV5640 live</title>
<style>
body { margin: 0; background: #111; color: #ddd; font: 16px sans-serif; }
img { width: 100%; height: auto; background: #000; }
p { margin: 8px 12px; }
</style>
<p>OV5640 on CAM0, 640x480 UYVY.</p>
<img src="/stream" alt="live">
"""
        body = page.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt: str, *args) -> None:
        return


def main() -> None:
    threading.Thread(target=capture, daemon=True).start()
    for _ in range(80):
        if latest:
            break
        time.sleep(0.1)
    print(f"http://0.0.0.0:{PORT}/", flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
