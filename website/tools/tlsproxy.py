#!/usr/bin/env python3
"""TLS-Proxy: lauscht auf 127.0.0.1:<port>, reicht an 127.0.0.1:<ziel> durch."""
import socket, ssl, sys, threading
lport, tport, cert, key = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3], sys.argv[4]
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(cert, key)
def pump(a, b):
    try:
        while True:
            d = a.recv(65536)
            if not d: break
            b.sendall(d)
    except OSError: pass
    for s in (a, b):
        try:
            if isinstance(s, ssl.SSLSocket): s.unwrap()
            else: s.shutdown(socket.SHUT_RDWR)
        except (OSError, ValueError): pass
def handle(c):
    try:
        s = ctx.wrap_socket(c, server_side=True)
        t = socket.create_connection(("127.0.0.1", tport))
    except OSError: return
    threading.Thread(target=pump, args=(s, t), daemon=True).start(); pump(t, s)
l = socket.socket(); l.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
l.bind(("127.0.0.1", lport)); l.listen(32)
while True:
    c, _ = l.accept(); threading.Thread(target=handle, args=(c,), daemon=True).start()
