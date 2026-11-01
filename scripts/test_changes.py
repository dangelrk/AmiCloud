#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ronny Krueger
"""Tests fuer action=changes (Phase 6 des Desktop-Plans) gegen einen
laufenden Server: Grabsteine bei Umbenennung, Seiten ohne geteilte
change_seq, Praefix, mtime wie list.

Aufruf:  AC_SERVER=http://127.0.0.1:8099 AC_USER=tester AC_PASS=... \\
         python3 scripts/test_changes.py
Legt /_chg_<pid>/ an und loescht es am Ende wieder."""
import base64
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

SERVER = os.environ["AC_SERVER"].rstrip("/")
AUTH = "Basic " + base64.b64encode(
    f'{os.environ["AC_USER"]}:{os.environ["AC_PASS"]}'.encode()).decode()
ROOT = f"/_chg_{os.getpid()}"
ok = fail = 0


def api(action, method="GET", data=None, **params):
    q = urllib.parse.urlencode({"action": action, **params})
    req = urllib.request.Request(f"{SERVER}/api.php?{q}", data=data, method=method,
                                 headers={"Authorization": AUTH})
    try:
        with urllib.request.urlopen(req) as r:
            return r.status, json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


def check(cond, what):
    global ok, fail
    if cond:
        ok += 1
    else:
        fail += 1
        print(f"  FEHLER: {what}")


def put(path, content):
    s, j = api("upload", "POST", content.encode(), path=path)
    assert s == 200, (path, s, j)


def changes(since, **kw):
    """all pages; returns (entries, pages, last next)"""
    out, pages = [], 0
    while True:
        s, j = api("changes", since=since, **kw)
        assert s == 200, (s, j)
        out += j["entries"]
        pages += 1
        since = j["next"]
        if not j["has_more"]:
            return out, pages, since


def rev():
    return api("status")[1]["global_revision"]


print("Aufbau")
api("mkdir", "POST", b"", path=ROOT)
api("mkdir", "POST", b"", path=f"{ROOT}/d")
put(f"{ROOT}/d/f.txt", "f")
put(f"{ROOT}/d/g.txt", "g")
put(f"{ROOT}/x.txt", "x")
s0 = rev()

print("Umbenennen eines Ordners: Grabsteine fuer die alten Pfade")
s, j = api("rename", "POST", b"", path=f"{ROOT}/d", to=f"{ROOT}/e")
check(s == 200, f"rename: {s} {j}")
ent, _, _ = changes(s0, path=ROOT)
by = {e["path"]: e for e in ent}
for p in ("d", "d/f.txt", "d/g.txt"):
    check(by.get(f"{ROOT}/{p}", {}).get("deleted") is True, f"Grabstein {p}: {by.get(f'{ROOT}/{p}')}")
for p in ("e", "e/f.txt", "e/g.txt"):
    check(by.get(f"{ROOT}/{p}", {}).get("deleted") is False, f"neu {p}: {by.get(f'{ROOT}/{p}')}")
check(len({e["change_seq"] for e in ent}) == 1, "eine change_seq fuer die ganze Umbenennung")

print("mtime wie list, MD5 vorhanden")
s, lst = api("list", path=f"{ROOT}/e")
lm = {f"{ROOT}/e/{e['name']}": e for e in lst["entries"]}
for p in (f"{ROOT}/e/f.txt", f"{ROOT}/e/g.txt"):
    check(by[p]["mtime"] == lm[p]["mtime"], f"mtime {p}: {by[p]['mtime']} != {lm[p]['mtime']}")
    check(by[p]["md5"] == lm[p]["md5"] and len(by[p]["md5"]) == 32, f"md5 {p}")

print("Seiten: limit=2 teilt die Gruppe der Umbenennung (6 Zeilen) nicht")
s1 = rev()
put(f"{ROOT}/y1.txt", "1")
put(f"{ROOT}/y2.txt", "2")
api("rename", "POST", b"", path=f"{ROOT}/e", to=f"{ROOT}/h")
put(f"{ROOT}/y3.txt", "3")
ent, pages, nxt = changes(s1, path=ROOT, limit=2)
seqs = [e["change_seq"] for e in ent]
check(seqs == sorted(seqs), "aufsteigend")
check(pages >= 3, f"mehrere Seiten ({pages})")
check(len(ent) == 3 + 6, f"alle Zeilen genau einmal ({len(ent)})")
check(nxt >= rev(), f"next {nxt} >= global_revision")
# a single page must contain whole groups
s, j = api("changes", since=s1, path=ROOT, limit=2)
page_seqs = {e["change_seq"] for e in j["entries"]}
for q in page_seqs:
    full = [e for e in ent if e["change_seq"] == q]
    part = [e for e in j["entries"] if e["change_seq"] == q]
    check(len(full) == len(part), f"Gruppe {q} ganz auf der Seite")
# a group larger than the limit comes whole
s, j = api("changes", since=s1 + 2, path=ROOT, limit=2)
check(len(j["entries"]) == 6 and j["has_more"], f"grosse Gruppe ganz: {len(j['entries'])}")

print("Praefix: nur darunter")
ent, _, _ = changes(s0, path=f"{ROOT}/h")
check(ent and all(e["path"] == f"{ROOT}/h" or e["path"].startswith(f"{ROOT}/h/") for e in ent),
      "nur /h und darunter")
ent2, _, _ = changes(s0, path=f"{ROOT}/h2")
check(all(not e["path"].startswith(f"{ROOT}/h/") for e in ent2), "/h2 ist nicht /h")

print("Loeschen und Wiederanlegen")
s2 = rev()
api("delete", "POST", b"", path=f"{ROOT}/x.txt")
ent, _, _ = changes(s2, path=ROOT)
check([e["deleted"] for e in ent if e["path"] == f"{ROOT}/x.txt"] == [True], "x.txt geloescht")
put(f"{ROOT}/x.txt", "neu")
ent, _, _ = changes(s2, path=ROOT)
check([e["deleted"] for e in ent if e["path"] == f"{ROOT}/x.txt"] == [False], "x.txt wieder da")

print("Umbenennen auf einen alten Grabstein-Pfad")
api("mkdir", "POST", b"", path=f"{ROOT}/k")
put(f"{ROOT}/k/old.txt", "o")
api("delete", "POST", b"", path=f"{ROOT}/k", recursive=1)
s3 = rev()
api("rename", "POST", b"", path=f"{ROOT}/h", to=f"{ROOT}/k")
ent, _, _ = changes(s3 - 1, path=ROOT)
by = {e["path"]: e for e in ent}
check(by.get(f"{ROOT}/k/old.txt", {}).get("deleted") is True,
      f"alter Grabstein bleibt: {by.get(f'{ROOT}/k/old.txt')}")
check(by.get(f"{ROOT}/k/f.txt", {}).get("deleted") is False, "verschobene Datei lebt")

print("Ordner ohne mkdir (nur per Upload darunter): Zeile und Grabstein")
s4 = rev()
put(f"{ROOT}/imp/a.txt", "a")
put(f"{ROOT}/imp/sub/b.txt", "b")
ent, _, _ = changes(s4, path=ROOT)
by = {e["path"]: e for e in ent}
for p in ("imp", "imp/sub"):
    check(by.get(f"{ROOT}/{p}", {}).get("deleted") is False and by[f"{ROOT}/{p}"]["type"] == "dir",
          f"Ordner {p} gemeldet: {by.get(f'{ROOT}/{p}')}")
s5 = rev()
api("delete", "POST", b"", path=f"{ROOT}/imp", recursive=1)
ent, _, _ = changes(s5, path=ROOT)
by = {e["path"]: e for e in ent}
check(by.get(f"{ROOT}/imp", {}).get("deleted") is True, f"Grabstein fuer imp selbst: {by.get(f'{ROOT}/imp')}")
check(by.get(f"{ROOT}/imp/sub/b.txt", {}).get("deleted") is True, "Grabstein imp/sub/b.txt")

print("Veralteter Grabstein eines Ordners, den es wieder gibt")
put(f"{ROOT}/p/a/f.txt", "f")
api("setcomment", "POST", b"", path=f"{ROOT}/p/a", comment="x")
api("delete", "POST", b"", path=f"{ROOT}/p/a", recursive=1)
put(f"{ROOT}/p/a/f.txt", "f2")
s6 = rev()
api("rename", "POST", b"", path=f"{ROOT}/p/a", to=f"{ROOT}/p/b")
ent, _, _ = changes(s6, path=ROOT)
by = {e["path"]: e for e in ent}
check(by.get(f"{ROOT}/p/b", {}).get("deleted") is False, f"p/b lebt: {by.get(f'{ROOT}/p/b')}")
check(by.get(f"{ROOT}/p/b/f.txt", {}).get("deleted") is False, "p/b/f.txt lebt")
check(by.get(f"{ROOT}/p/a", {}).get("deleted") is True, "p/a geloescht")

print("Umbenennen nimmt alte Grabsteine nicht mit")
put(f"{ROOT}/q/t.txt", "t")
put(f"{ROOT}/q/u.txt", "u")
api("delete", "POST", b"", path=f"{ROOT}/q/t.txt")
s7 = rev()
api("rename", "POST", b"", path=f"{ROOT}/q", to=f"{ROOT}/q2")
ent, _, _ = changes(s7, path=ROOT)
paths = {e["path"] for e in ent}
check(f"{ROOT}/q2/t.txt" not in paths, "kein Grabstein q2/t.txt")
check(f"{ROOT}/q2/u.txt" in paths and f"{ROOT}/q/u.txt" in paths, "u.txt alt und neu")

print("Aufraeumen")
api("delete", "POST", b"", path=ROOT, recursive=1)
print(f"\n{ok} ok, {fail} Fehler")
sys.exit(1 if fail else 0)
