#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ronny Krueger
# AmiCloud API-Testsuite — prueft 11 der 13 Actions (alle ausser changes und
# search) gegen einen laufenden Server. Beschrieben in docs/api/API.md.
#
# Aufruf:   scripts/test_api.sh [config-datei]
# Default-Config: amicloud.cfg im Repo-Root (server=/user=/pass=).
# Alternativ per Umgebung: AC_SERVER, AC_USER, AC_PASS.
#
# Benoetigt: curl, python3 (fuer JSON-Pruefungen).

set -u

CFG="${1:-$(dirname "$0")/../amicloud.cfg}"
if [ -f "$CFG" ]; then
    AC_SERVER="${AC_SERVER:-$(sed -n 's/^server=//p'   "$CFG" | head -1)}"
    AC_USER="${AC_USER:-$(sed -n 's/^user\(name\)\{0,1\}=//p' "$CFG" | head -1)}"
    AC_PASS="${AC_PASS:-$(sed -n 's/^pass\(word\)\{0,1\}=//p' "$CFG" | head -1)}"
fi
: "${AC_SERVER:?AC_SERVER fehlt (server= in Config oder Umgebung)}"
: "${AC_USER:?AC_USER fehlt}"
: "${AC_PASS:?AC_PASS fehlt}"

API="$AC_SERVER/api.php"
AUTH="$AC_USER:$AC_PASS"
TDIR="/_apitest_$$"
PASSED=0
FAILED=0

req() {  # req <method> <query> [curl-optionen...]
    method=$1; query=$2; shift 2
    curl -sS -u "$AUTH" -X "$method" "$@" "$API?$query"
}

check() {  # check <name> <ausgabe> <python-ausdruck ueber j>
    name=$1; out=$2; expr=$3
    if printf '%s' "$out" | python3 -c "
import json,sys
try:
    j = json.load(sys.stdin)
except Exception:
    sys.exit(1)
sys.exit(0 if ($expr) else 1)
"; then
        PASSED=$((PASSED+1)); echo "ok    $name"
    else
        FAILED=$((FAILED+1)); echo "FAIL  $name  ->  $out"
    fi
}

check_raw() {  # check_raw <name> <ist> <soll>
    name=$1; got=$2; want=$3
    if [ "$got" = "$want" ]; then
        PASSED=$((PASSED+1)); echo "ok    $name"
    else
        FAILED=$((FAILED+1)); echo "FAIL  $name  ->  '$got' != '$want'"
    fi
}

echo "== AmiCloud API-Tests gegen $API (Testordner $TDIR) =="

# --- status ----------------------------------------------------------------
out=$(req GET "action=status")
check "status liefert version" "$out" "j['status']=='ok' and 'version' in j"

# --- mkdir ---------------------------------------------------------------
out=$(req POST "action=mkdir&path=$TDIR")
check "mkdir $TDIR" "$out" "j['status']=='ok'"

out=$(req POST "action=mkdir&path=$TDIR")
check "mkdir existiert -> 409" "$out" "j['status']=='error' and j['code']==409"

# --- upload --------------------------------------------------------------
out=$(req POST "action=upload&path=$TDIR/hello.txt" --data-binary 'Hello AmiCloud!')
check "upload hello.txt" "$out" "j['status']=='ok' and j['size']==15"

out=$(req POST "action=upload&path=$TDIR/leer.txt" -H 'Content-Length: 0')
check "upload leere Datei (0 Byte)" "$out" "j['status']=='ok' and j['size']==0"
req DELETE "action=delete&path=$TDIR/leer.txt" >/dev/null

# --- MD5-Pruefsummen (Abschnitt 18) ------------------------------------------
HELLO_MD5=$(printf '%s' 'Hello AmiCloud!' | python3 -c "import hashlib,sys; print(hashlib.md5(sys.stdin.buffer.read()).hexdigest())")

out=$(req POST "action=upload&path=$TDIR/md5ok.txt&md5=$HELLO_MD5" --data-binary 'Hello AmiCloud!')
check "upload mit korrektem &md5=" "$out" "j['status']=='ok' and j['md5']=='$HELLO_MD5'"

code=$(curl -sS -o /dev/null -w '%{http_code}' -u "$AUTH" -X POST \
    --data-binary 'Hello AmiCloud!' \
    "$API?action=upload&path=$TDIR/md5bad.txt&md5=deadbeefdeadbeefdeadbeefdeadbeef")
check_raw "upload mit falschem &md5= -> 422" "$code" "422"

out=$(req GET "action=info&path=$TDIR/md5ok.txt")
check "info liefert md5 + mtime" "$out" \
    "j['status']=='ok' and j['md5']=='$HELLO_MD5' and isinstance(j['mtime'], int) and j['mtime']>0"

req DELETE "action=delete&path=$TDIR/md5ok.txt" >/dev/null

# --- list ----------------------------------------------------------------
out=$(req GET "action=list&path=$TDIR")
check "list zeigt hello.txt + Metafelder" "$out" \
    "j['status']=='ok' and j['entries'][0]['name']=='hello.txt' and 'protection' in j['entries'][0] and 'comment' in j['entries'][0] and j['entries'][0]['md5']=='$HELLO_MD5' and isinstance(j['entries'][0]['mtime'], int)"

# --- info ----------------------------------------------------------------
out=$(req GET "action=info&path=$TDIR/hello.txt")
check "info hello.txt" "$out" \
    "j['status']=='ok' and j['size']==15 and j['type']=='file' and 'protection' in j and 'comment' in j and j['md5']=='$HELLO_MD5'"

# --- download (ganz + Teildownload) ---------------------------------------
hdrs=$(curl -sS -D - -o /tmp/_amicloud_test_dl.$$ -u "$AUTH" "$API?action=download&path=$TDIR/hello.txt")
check_raw "download X-AmiCloud-MD5 Header" \
    "$(printf '%s' "$hdrs" | tr -d '\r' | sed -n 's/^[Xx]-[Aa]mi[Cc]loud-[Mm][Dd]5: //p')" "$HELLO_MD5"
check_raw "download komplett (Body)" "$(cat /tmp/_amicloud_test_dl.$$)" "Hello AmiCloud!"
rm -f /tmp/_amicloud_test_dl.$$

hdrs=$(curl -sS -D - -o /dev/null -u "$AUTH" "$API?action=download&path=$TDIR/hello.txt&offset=6&length=8")
check_raw "Teil-Download hat KEINEN X-AmiCloud-MD5 Header" \
    "$(printf '%s' "$hdrs" | tr -d '\r' | grep -ic '^x-amicloud-md5:')" "0"

got=$(req GET "action=download&path=$TDIR/hello.txt")
check_raw "download komplett" "$got" "Hello AmiCloud!"

got=$(req GET "action=download&path=$TDIR/hello.txt&offset=6&length=8")
check_raw "download offset=6 length=8" "$got" "AmiCloud"

# Range: so setzen die Clients einen abgebrochenen Download fort
hdrs=$(curl -sS -D - -o /tmp/_amicloud_test_dl.$$ -u "$AUTH" -H "Range: bytes=6-" "$API?action=download&path=$TDIR/hello.txt")
check_raw "download Range bytes=6- -> 206 + Content-Range" \
    "$(printf '%s' "$hdrs" | tr -d '\r' | sed -n -e 's/^HTTP\/[0-9.]* \([0-9]*\).*/\1/p' -e 's/^[Cc]ontent-[Rr]ange: //p' | tr '\n' ' ')" \
    "206 bytes 6-14/15 "
check_raw "download Range (Body)" "$(cat /tmp/_amicloud_test_dl.$$)" "AmiCloud!"
rm -f /tmp/_amicloud_test_dl.$$
got=$(curl -sS -o /dev/null -w '%{http_code}' -u "$AUTH" -H "Range: bytes=99-" "$API?action=download&path=$TDIR/hello.txt")
check_raw "download Range hinter Dateiende -> 416" "$got" "416"

# --- setcomment / setprotect / setdate ------------------------------------
out=$(req POST "action=setcomment&path=$TDIR/hello.txt&comment=Testkommentar")
check "setcomment" "$out" "j['status']=='ok' and j['comment']=='Testkommentar'"

out=$(req POST "action=setprotect&path=$TDIR/hello.txt&bits=13")
check "setprotect bits=13" "$out" "j['status']=='ok' and j['protection']==13"

out=$(req POST "action=setdate&path=$TDIR/hello.txt&modified=2020-05-01%2012:30:00")
check "setdate" "$out" "j['status']=='ok'"

out=$(req GET "action=info&path=$TDIR/hello.txt")
check "info zeigt comment/protection/datum" "$out" \
    "j['comment']=='Testkommentar' and j['protection']==13 and j['modified']=='2020-05-01 12:30:00'"

out=$(req POST "action=setdate&path=$TDIR/hello.txt&modified=bogus")
check "setdate ungueltig -> 400" "$out" "j['status']=='error' and j['code']==400"

# --- setdate&mtime= (UTC-Epoche, Abschnitt 19) -------------------------------
out=$(req POST "action=setdate&path=$TDIR/hello.txt&mtime=946684800")
check "setdate&mtime=946684800" "$out" "j['status']=='ok' and j['mtime']==946684800"

out=$(req GET "action=info&path=$TDIR/hello.txt")
check "info nach setdate&mtime= konsistent" "$out" "j['mtime']==946684800"

# --- rename Datei ----------------------------------------------------------
out=$(req POST "action=rename&path=$TDIR/hello.txt&to=$TDIR/renamed.txt")
check "rename Datei" "$out" "j['status']=='ok'"

out=$(req GET "action=info&path=$TDIR/renamed.txt")
check "info renamed.txt behaelt Metadaten" "$out" \
    "j['status']=='ok' and j['comment']=='Testkommentar' and j['protection']==13"

out=$(req GET "action=info&path=$TDIR/hello.txt")
check "alter Name weg -> 404" "$out" "j['status']=='error' and j['code']==404"

# --- rename Verzeichnis (rekursives DB-Update) ------------------------------
req POST "action=mkdir&path=$TDIR/sub" >/dev/null
req POST "action=upload&path=$TDIR/sub/child.txt" --data-binary 'kind' >/dev/null
req POST "action=setcomment&path=$TDIR/sub/child.txt&comment=Kindnotiz" >/dev/null

out=$(req POST "action=rename&path=$TDIR/sub&to=$TDIR/sub2")
check "rename Verzeichnis" "$out" "j['status']=='ok'"

out=$(req GET "action=info&path=$TDIR/sub2/child.txt")
check "Kind-Metadaten nach Dir-Rename" "$out" \
    "j['status']=='ok' and j['comment']=='Kindnotiz'"

out=$(req POST "action=rename&path=$TDIR/sub2&to=$TDIR/sub2")
check "rename auf sich selbst -> 409" "$out" "j['status']=='error' and j['code']==409"

# --- rekursive Verzeichnisgroesse (info) + rekursives Loeschen -------------
req POST "action=mkdir&path=$TDIR/rsize" >/dev/null
req POST "action=mkdir&path=$TDIR/rsize/sub" >/dev/null
req POST "action=upload&path=$TDIR/rsize/a.txt" --data-binary '12345' >/dev/null
req POST "action=upload&path=$TDIR/rsize/sub/b.txt" --data-binary '1234567890' >/dev/null

out=$(req GET "action=info&path=$TDIR/rsize")
check "info liefert rekursive Verzeichnisgroesse (5+10 Byte)" "$out" \
    "j['status']=='ok' and j['type']=='dir' and j['size']==15"

out=$(req DELETE "action=delete&path=$TDIR/rsize")
check "delete nicht-leeres Verzeichnis ohne recursive -> 409" "$out" \
    "j['status']=='error' and j['code']==409"

out=$(req DELETE "action=delete&path=$TDIR/rsize&recursive=1")
check "delete recursive=1 loescht kompletten Baum" "$out" "j['status']=='ok'"

out=$(req GET "action=info&path=$TDIR/rsize")
check "rsize nach recursive delete verschwunden -> 404" "$out" \
    "j['status']=='error' and j['code']==404"

# --- Sicherheit -------------------------------------------------------------
out=$(req GET "action=list&path=..%2F..%2Fetc")
check "Path-Traversal -> 400" "$out" "j['status']=='error' and j['code']==400"

# --- Benutzerverwaltung (Server ab migrate_004) ----------------------------
out=$(req GET "action=status")
check "status liefert Konto und Kontingent" "$out" \
    "j['user']['name']!='' and j['quota']['used']>=0 and j['quota']['free']>=0 and j['quota']['total']>=j['quota']['used']"

out=$(req DELETE "action=delete&path=/&recursive=1")
check "delete Stammordner -> 400" "$out" "j['status']=='error' and j['code']==400"

req POST "action=mkdir&path=$TDIR/loop" >/dev/null
out=$(req POST "action=rename&path=$TDIR/loop&to=$TDIR/loop/innen")
check "Ordner in sich selbst verschieben -> 400" "$out" "j['status']=='error' and j['code']==400"
req DELETE "action=delete&path=$TDIR/loop" >/dev/null

code=$(curl -sS -o /dev/null -w '%{http_code}' "$API?action=list&path=/")
check_raw "ohne Auth -> 401" "$code" "401"

# --- Amiga-Modus (migrate_007): neue Namen <= 30 Zeichen --------------------
N30=abcdefghijklmnopqrstuvwxyz_1.c     # 30 Zeichen
N31=abcdefghijklmnopqrstuvwxyz_12.c    # 31 Zeichen
NMAX=$(req GET "action=status" | python3 -c "import json,sys; print(json.load(sys.stdin).get('name_max', 0))")
if [ "$NMAX" = 30 ]; then
    out=$(req POST "action=upload&path=$TDIR/$N31" --data-binary 'x')
    check "Amiga-Modus: upload 31 Zeichen -> 400" "$out" "j['status']=='error' and j['code']==400"
    out=$(req POST "action=upload&path=$TDIR/$N30" --data-binary 'x')
    check "Amiga-Modus: upload 30 Zeichen ok" "$out" "j['status']=='ok'"
    out=$(req POST "action=upload&path=$TDIR/$N30" --data-binary 'y')
    check "Amiga-Modus: vorhandene Datei ueberschreiben ok" "$out" "j['status']=='ok'"
    out=$(req POST "action=mkdir&path=$TDIR/$N31")
    check "Amiga-Modus: mkdir 31 Zeichen -> 400" "$out" "j['status']=='error' and j['code']==400"
    out=$(req POST "action=upload&path=$TDIR/$N31/in.txt" --data-binary 'x')
    check "Amiga-Modus: neuer langer Ordner per upload -> 400" "$out" "j['status']=='error' and j['code']==400"
    out=$(req POST "action=rename&path=$TDIR/$N30&to=$TDIR/$N31")
    check "Amiga-Modus: rename auf 31 Zeichen -> 400" "$out" "j['status']=='error' and j['code']==400"
    req POST "action=mkdir&path=$TDIR/ziel" >/dev/null
    out=$(req POST "action=rename&path=$TDIR/$N30&to=$TDIR/ziel/$N30")
    check "Amiga-Modus: verschieben behaelt Namen ok" "$out" "j['status']=='ok'"
    req DELETE "action=delete&path=$TDIR/ziel&recursive=1" >/dev/null
else
    echo "--    Amiga-Modus-Tests uebersprungen (name_max=$NMAX: lange Namen erlaubt oder alter Server)"
fi

# --- Aufraeumen --------------------------------------------------------------
req DELETE "action=delete&path=$TDIR/renamed.txt" >/dev/null
req DELETE "action=delete&path=$TDIR/sub2/child.txt" >/dev/null
req DELETE "action=delete&path=$TDIR/sub2" >/dev/null
out=$(req DELETE "action=delete&path=$TDIR")
check "delete Testordner" "$out" "j['status']=='ok'"

echo "== Ergebnis: $PASSED ok, $FAILED fehlgeschlagen =="
[ "$FAILED" -eq 0 ]
