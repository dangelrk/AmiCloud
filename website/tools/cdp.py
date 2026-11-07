#!/usr/bin/env python3
"""Kleiner CDP-Client (nur stdlib) fuer headless Chrome."""
import base64, json, os, socket, struct, subprocess, sys, tempfile, time, urllib.request
class Tab:
    def __init__(self, port):
        t = json.load(urllib.request.urlopen(urllib.request.Request(f"http://127.0.0.1:{port}/json/new?about:blank", method="PUT")))
        path = t["webSocketDebuggerUrl"].split(str(port), 1)[1]
        self.s = socket.create_connection(("127.0.0.1", port))
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(f"GET {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
        b = b""
        while b"\r\n\r\n" not in b: b += self.s.recv(4096)
        self.n = 0
    def _send(self, txt):
        d = txt.encode(); h = b"\x81"; m = os.urandom(4); l = len(d)
        h += bytes([0x80 | l]) if l < 126 else bytes([0xfe]) + struct.pack(">H", l) if l < 65536 else bytes([0xff]) + struct.pack(">Q", l)
        self.s.sendall(h + m + bytes(c ^ m[i % 4] for i, c in enumerate(d)))
    def _rx(self, n):
        b = b""
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c: raise EOFError
            b += c
        return b
    def _recv(self):
        out = b""
        while True:
            a, l = self._rx(2); fin = a & 0x80; l &= 0x7f
            if l == 126: l = struct.unpack(">H", self._rx(2))[0]
            elif l == 127: l = struct.unpack(">Q", self._rx(8))[0]
            out += self._rx(l)
            if fin: return out.decode()
    def call(self, method, **p):
        self.n += 1; self._send(json.dumps({"id": self.n, "method": method, "params": p}))
        while True:
            m = json.loads(self._recv())
            if m.get("id") == self.n:
                if "error" in m: raise RuntimeError(m["error"])
                return m["result"]
    def js(self, expr):
        r = self.call("Runtime.evaluate", expression=expr, returnByValue=True, awaitPromise=True)
        if "exceptionDetails" in r: raise RuntimeError(r["exceptionDetails"])
        return r["result"].get("value")
    def goto(self, url, wait=0.6):
        self.call("Page.navigate", url=url); self.settle(wait)
    def settle(self, wait=0.6):
        for _ in range(100):
            if self.js("document.readyState") == "complete": break
            time.sleep(0.1)
        time.sleep(wait)
    def size(self, w, h, dpr=2, mobile=False):
        self.call("Emulation.setDeviceMetricsOverride", width=w, height=h, deviceScaleFactor=dpr, mobile=mobile)
    def dark(self, on):
        self.call("Emulation.setEmulatedMedia", features=[{"name": "prefers-color-scheme", "value": "dark" if on else "light"}])
    def shot(self, path, full=False):
        r = self.call("Page.captureScreenshot", format="png", captureBeyondViewport=full)
        open(path, "wb").write(base64.b64decode(r["data"]))
def start(port=9333):
    prof = tempfile.mkdtemp(prefix="cdp-")
    p = subprocess.Popen(["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "--headless=new", f"--remote-debugging-port={port}", f"--user-data-dir={prof}", "--no-first-run", "--hide-scrollbars", "--remote-allow-origins=*", "--ignore-certificate-errors", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(100):
        try: urllib.request.urlopen(f"http://127.0.0.1:{port}/json/version", timeout=1); break
        except OSError: time.sleep(0.2)
    return p
