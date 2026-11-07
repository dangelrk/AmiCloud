#!/usr/bin/env python3
"""Mini-WebDAV im ownCloud-Pfadschema, nur fuer Screenshots. Basic-Auth demo."""
import base64, email.utils, hashlib, os, shutil, sys, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from xml.sax.saxutils import escape
ROOT, PORT = sys.argv[1], int(sys.argv[2])
PFX = "/remote.php/dav/files/demo"
AUTH = "Basic " + base64.b64encode(b"demo:demo-passwort-1").decode()
def etag(p):
    st = os.stat(p); return hashlib.md5(f"{st.st_mtime_ns}{st.st_size}".encode()).hexdigest()
class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def fs(self, path=None):
        u = urllib.parse.unquote(urllib.parse.urlparse(path or self.path).path)
        if not u.startswith(PFX): return None
        r = os.path.normpath(u[len(PFX):].lstrip("/") or ".")
        if r.startswith(".."): return None
        return os.path.join(ROOT, r)
    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""
    def out(self, code, data=b"", ct="text/plain", extra=None):
        self.send_response(code); self.send_header("Content-Type", ct)
        self.send_header("Content-Length", str(len(data)))
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD": self.wfile.write(data)
    def gate(self):
        self.req = self.body() if self.command != "PUT" else None
        if self.headers.get("Authorization") != AUTH:
            if self.req is None: self.body()
            self.out(401, extra={"WWW-Authenticate": 'Basic realm="demo"'}); return None
        p = self.fs()
        if p is None:
            self.out(404); return None
        return p
    def do_OPTIONS(self):
        self.body(); self.out(200, extra={"DAV": "1,2", "Allow": "OPTIONS,GET,HEAD,PUT,DELETE,PROPFIND,MKCOL,MOVE,COPY"})
    def entry(self, p):
        rel = os.path.relpath(p, ROOT); rel = "" if rel == "." else rel
        href = PFX + "/" + urllib.parse.quote(rel) + ("/" if os.path.isdir(p) and rel else "")
        if not rel: href = PFX + "/"
        st = os.stat(p); m = email.utils.formatdate(st.st_mtime, usegmt=True)
        typ = "<d:resourcetype><d:collection/></d:resourcetype>" if os.path.isdir(p) else f"<d:resourcetype/><d:getcontentlength>{st.st_size}</d:getcontentlength>"
        return f'<d:response><d:href>{escape(href)}</d:href><d:propstat><d:prop><d:getlastmodified>{m}</d:getlastmodified><d:getetag>"{etag(p)}"</d:getetag>{typ}</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>'
    def do_PROPFIND(self):
        p = self.gate()
        if p is None: return
        if not os.path.exists(p): return self.out(404)
        items = [p]
        if self.headers.get("Depth", "1") != "0" and os.path.isdir(p):
            items += [os.path.join(p, n) for n in sorted(os.listdir(p)) if not n.startswith(".")]
        x = '<?xml version="1.0"?><d:multistatus xmlns:d="DAV:">' + "".join(self.entry(i) for i in items) + "</d:multistatus>"
        self.out(207, x.encode(), "application/xml; charset=utf-8")
    def do_GET(self):
        p = self.gate()
        if p is None: return
        if not os.path.isfile(p): return self.out(404)
        self.out(200, open(p, "rb").read(), "application/octet-stream", {"ETag": f'"{etag(p)}"', "Last-Modified": email.utils.formatdate(os.stat(p).st_mtime, usegmt=True)})
    do_HEAD = do_GET
    def do_PUT(self):
        p = self.gate()
        if p is None: return
        d = self.body(); os.makedirs(os.path.dirname(p), exist_ok=True); open(p, "wb").write(d)
        self.out(201, extra={"ETag": f'"{etag(p)}"'})
    def do_MKCOL(self):
        p = self.gate()
        if p is None: return
        if os.path.exists(p): return self.out(405)
        os.makedirs(p); self.out(201)
    def do_DELETE(self):
        p = self.gate()
        if p is None: return
        if not os.path.exists(p): return self.out(404)
        shutil.rmtree(p) if os.path.isdir(p) else os.remove(p); self.out(204)
    def do_MOVE(self):
        p = self.gate()
        if p is None: return
        d = self.fs(urllib.parse.urlparse(self.headers.get("Destination", "")).path)
        if d is None: return self.out(400)
        os.makedirs(os.path.dirname(d), exist_ok=True); shutil.move(p, d); self.out(201)
ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
