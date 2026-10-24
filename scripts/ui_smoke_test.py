"""Smoke test for `wkc-loader --serve`: drives the UI protocol over a raw WebSocket.

Usage: python3 -I scripts/ui_smoke_test.py [--port 8000] [--out frame.png]
Starts nothing itself; run `scripts/wkc.sh --serve <port> --html scratch/input.html` first.
"""
import argparse
import base64
import json
import os
import socket
import struct
import time
import urllib.request
import zlib


class Ws:
    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port))
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f"GET /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            resp += self.s.recv(1)
        assert b" 101 " in resp, resp

    def send(self, obj):
        data = json.dumps(obj).encode()
        mask = os.urandom(4)
        hdr = bytes([0x81])
        n = len(data)
        hdr += bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack(">H", n)
        self.s.sendall(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.s.recv(n - len(buf))
            if not chunk:
                raise EOFError
            buf += chunk
        return buf

    def recv(self, timeout):
        self.s.settimeout(timeout)
        try:
            b0, b1 = self.recv_exact(2)
        except (socket.timeout, TimeoutError):
            return None
        n = b1 & 0x7F
        if n == 126:
            n = struct.unpack(">H", self.recv_exact(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", self.recv_exact(8))[0]
        self.s.settimeout(30)
        payload = self.recv_exact(n)
        return ("text", json.loads(payload)) if b0 & 0x0F == 1 else ("binary", payload)


def write_png(path, w, h, rgba):
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--out", default="ui-frame.png")
    args = ap.parse_args()

    html = urllib.request.urlopen(f"http://127.0.0.1:{args.port}/").read()
    print("GET / ->", len(html), "bytes,", "has canvas" if b"<canvas" in html else "NO CANVAS")

    ws = Ws(args.port)
    fb = bytearray(1280 * 720 * 4)
    frames, messages = 0, []

    def pump(seconds):
        nonlocal frames
        end = time.time() + seconds
        while time.time() < end:
            m = ws.recv(max(0.05, end - time.time()))
            if m is None:
                continue
            kind, data = m
            if kind == "binary":
                x, y, w, h = struct.unpack_from("<4I", data, 0)
                for row in range(h):
                    src = data[16 + row * w * 4: 16 + (row + 1) * w * 4]
                    off = ((y + row) * 1280 + x) * 4
                    fb[off:off + w * 4] = src
                frames += 1
            else:
                messages.append(data)

    pump(3)
    print("after connect:", frames, "frame(s);", [m for m in messages if m["t"] == "state"][:1])
    # Click into the text field, type, click the button: the page sets document.title.
    ws.send({"t": "mouse", "k": "down", "x": 300, "y": 60, "b": 0, "m": 0})
    ws.send({"t": "mouse", "k": "up", "x": 300, "y": 60, "b": 0, "m": 0})
    pump(1)
    for ch in "hello switch":
        ws.send({"t": "key", "k": "down", "code": ord(ch.upper()) if ch != " " else 32, "m": 0, "r": False})
        ws.send({"t": "text", "c": ord(ch)})
        ws.send({"t": "key", "k": "up", "code": ord(ch.upper()) if ch != " " else 32, "m": 0, "r": False})
    pump(1.5)
    ws.send({"t": "mouse", "k": "down", "x": 700, "y": 60, "b": 0, "m": 0})
    ws.send({"t": "mouse", "k": "up", "x": 700, "y": 60, "b": 0, "m": 0})
    pump(1.5)
    ws.send({"t": "wheel", "x": 600, "y": 400, "dx": 0, "dy": -600, "m": 0})
    pump(1.5)
    ws.send({"t": "eval", "js": "document.title + ' | scrollY=' + scrollY + ' | value=' + document.getElementById('q').value"})
    pump(1.5)
    write_png(args.out, 1280, 720, bytes(fb))
    print("frames:", frames)
    for m in messages:
        if m["t"] in ("title", "eval", "console", "url", "loading", "stub"):
            print(" ", json.dumps(m)[:160])
    print("wrote", args.out)


if __name__ == "__main__":
    main()
