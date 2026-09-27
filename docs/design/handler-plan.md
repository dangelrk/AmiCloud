# AmiCloud DOS-Handler `CLOUD:` — Stufenplan (3 Commits)

## Context

Der AmiCloud-Client läuft zu 100 % auf dem Amiga. Jetzt soll ein AmigaOS-3.x-DOS-Handler den
Cloud-Speicher wie ein Laufwerk `CLOUD:` verfügbar machen: Dateien/Ordner rein-/rauskopieren,
löschen, anlegen, auflisten. Drei Stufen, jede als eigener Git-Commit im neuen Ordner `cloudhandler/`.
Kein Basis-Commit des restlichen Codes (Nutzer-Entscheidung); Stufe-2/3-Commits nehmen die
mitgeänderten `client/cmd.c`/`cmd.h` mit auf.

**Entscheidende Vorerfahrung** (früherer, gelöschter Handler, 2026-07-04): Ein-Prozess-Design
crashte mit Guru 0x87000004 (AN_DOSLib+4), weil `pr_MsgPort` gleichzeitig DOS-Packet-Port und
DoPkt-Reply-Port war. Bewährt: **Zwei Prozesse** — Handler (Packet-Loop, nach `dn_Task`-Setzen
ausschließlich exec.library) + Worker (normales Programm mit bsdsocket/AmiSSL), Rendezvous über
benannten Port, Puffer über die Prozessgrenze immer AllocVec/FreeVec.
`client/http_amiga.c` ist bereits handler-tauglich: `http_warmup()`, `http_log_bptr`,
`g_log_buf`-Hooks existieren (client/http.h:116-128).

**Nutzer-Entscheidungen:** Device `CLOUD:` · kein Basis-Commit · Lesen = ganze Datei beim
Öffnen in RAM-Puffer (Server kann zwar Byte-Ranges via `download&offset=&length=`, aber
1 TLS-Handshake pro READ wäre auf 68000 zu langsam; Range-Fallback als dokumentierte Zukunftsoption).

> **Ordner-Regel (verbindlich):** Der neue Handler-Code entsteht ausschließlich in
> `cloudhandler/`. Der bestehende Ordner `handler/` (falls vorhanden) darf **nicht angefasst
> und nicht einmal angeschaut/gelistet** werden — kein `ls`, `Read`, `grep`, `cd` o.ä. darauf.
> Er ist für Claude in dieser Aufgabe komplett tabu.

**Server-API** (Basic-Auth pro Request, HTTPS): `status, list, download, upload, delete, info,
mkdir, rename, setcomment, setprotect, setdate`. `list`-Einträge tragen `name/type/size/mtime
(UTC-Epoch)/md5/protection (Amiga-Bits)/comment`. **Upload = nur Ganzdatei-Overwrite** (kein
Append/Partial) → Schreiben lokal puffern, Upload bei ACTION_END.

## Dateilayout `cloudhandler/` (S1/S2/S3 = Einführungsstufe)

```
cloudhandler/
  handler.c       S1  Eigener Entry (_start, -nostdlib!), ACTION_STARTUP,
                      Volume-Node, Packet-Loop, Dispatch, reply via PutMsg
  actions.c/.h    S1  Eine Funktion pro ACTION_*
  vol.c/.h        S1  RAM-Baum (CNode); ab S2 = Cache des Server-Zustands
  util.c/.h       S1  BSTR<->C-String, Latin-1-Case-Insensitive-Vergleich,
                      Pfad-Split, DateStamp<->Epoch
  proto.h         S2  WREQ_*-Codes + struct WorkerMsg (Handler<->Worker)
  worker.c        S2  amicloud-worker: config_load, http_init/warmup,
                      Port "AmiCloud-Worker", Request-Loop, stille ui_hooks
  backend.c/.h    S2  Handler-Seite: send_wreq() synchron, Cache-TTL,
                      HTTP->DOS-Fehler-Mapping
  Makefile.amiga  S1  vc +aos68k -cpu=68000 (wie client/Makefile.amiga)
  mount/CLOUD     S1  DOSDrivers-Beispiel
  README.md       S1  Build/Install/Mount/Test, wächst pro Stufe
```

---

## Stufe 1 — RAM-POC (Commit 1)

### Prozess-Disziplin (wichtigste Regel)
- `handler.c` mit **eigenem Entry, `-nostdlib`** (vbcc-Startup würde als Nicht-CLI-Prozess das
  Startup-Packet als WBenchMsg fressen). `SysBase = *(struct ExecBase **)4L`. Kein stdio/malloc,
  eigene mini `str*/mem*` in util.c. Stack via Mountlist `StackSize=16384`.
- Startup-Phase (dos.library noch erlaubt): Startup-Packet holen (`dp_Arg3` = BPTR DeviceNode),
  `MakeDosEntry("AmiCloud", DLT_VOLUME)` + `AddDosEntry` (→ Workbench-Icon),
  Root-CNode bauen, **dann** `dn_Task = &pr_MsgPort` setzen, Startup-Packet DOSTRUE beantworten.
- Danach **nur exec.library** (WaitPort/GetMsg/PutMsg/AllocVec/CopyMem/Forbid/FindPort…),
  Replies über eigenes `reply_pkt()` mit rohem `PutMsg` — nie ReplyPkt/DoPkt/Delay.

### RAM-Baum (vol.c)
`CNode { parent/next/children, type, name[108], data/size/alloc, protection, comment[80],
DateStamp, lock_count, excl_lock, open_count, seq }`
- Namen Latin-1-case-insensitiv (256er-Uppercase-Tabelle, nicht ASCII-toupper), max 106 Zeichen.
- Datei-Daten: AllocVec, Wachstum durch Verdoppeln (Start 16 KB), kein realloc.
- Locks: AllocVec'te `struct FileLock`, `fl_Key = (LONG)node`; **BPTR 0 = Root**; exklusiv nur
  wenn `lock_count==0 && !excl_lock && open_count==0`, sonst `ERROR_OBJECT_IN_USE`.
- FileHandles: DOS liefert FileHandle-BPTR in dp_Arg1, Handler setzt nur `fh_Arg1` auf eigenes
  `OpenFile { node, pos, writable, dirty }`.
- Pfadauflösung `resolve_path(lockdir, name, &parent, &leaf)`: BSTR (Längenbyte!) → C-String;
  alles bis inkl. `:` strippen → Root; **leere `/`-Komponente = Parent** (Amiga-Semantik!).
- ExNext-Stabilität: `fib_DiskKey` = monotone `seq` pro Verzeichnis, nie rohe Pointer;
  Ende → `ERROR_NO_MORE_ENTRIES`.
- FIB füllen: `fib_FileName`/`fib_Comment` **BCPL-Style** (Längenbyte voran);
  `fib_DirEntryType` UND `fib_EntryType` beide setzen (ST_ROOT/ST_USERDIR/ST_FILE).

### Implementierte Packets (Stufe 1, alle in RAM)
STARTUP, IS_FILESYSTEM, LOCATE_OBJECT, FREE_LOCK, COPY_DIR, COPY_DIR_FH, PARENT, PARENT_FH,
SAME_LOCK, EXAMINE_OBJECT, EXAMINE_NEXT, EXAMINE_FH, FINDINPUT/FINDOUTPUT/FINDUPDATE,
READ, WRITE, SEEK, END, SET_FILE_SIZE, CREATE_DIR, DELETE_OBJECT, RENAME_OBJECT,
SET_PROTECT, SET_COMMENT, SET_DATE, DISK_INFO, INFO, FH_FROM_LOCK, CHANGE_MODE, FLUSH,
CURRENT_VOLUME. Unbekanntes (inkl. EXAMINE_ALL) → `ERROR_ACTION_NOT_KNOWN` (DOS fällt auf
ExNext zurück). Arg-Konventionen gegen `Include_H/dos/dosextens.h` (ACTION_* Z. 152-227) und
`rkrm/rkrm-dos.pdf` prüfen — als Kommentarblock in actions.c zitieren.

### Mount (`cloudhandler/mount/CLOUD`)
```
Handler   = L:amicloud-handler
StackSize = 16384
Priority  = 5
GlobVec   = -1      /* Pflicht für C-Handler — kein BCPL-GlobVec */
Activate  = 1
```

### Test Stufe 1 (echter Amiga, ohne Netz)
Build (`vc +aos68k -nostdlib`, handler.o zuerst im Link), nach `L:` kopieren, mounten.
Dann: `Dir CLOUD:` → `MakeDir` → `Copy S:Startup-Sequence CLOUD:Test/` → `Type` → Rückkopie +
Vergleich → `Rename` → `Protect +p` → `FileNote` → `List` (zeigt Schutz+Kommentar) → `Delete`,
`Delete … ALL` → Workbench-Fenster (Icon, Drag&Drop, Schublade anlegen) →
`Copy SYS:Utilities CLOUD: ALL` (Stress) → paralleles `Dir` während `Copy` (ExNext-Stabilität).

---

## Stufe 2 — Worker-Prozess + echtes Backend (Commit 2)

### Architektur
- Handler bleibt exec-only; `vol.c`-Baum wird **Cache** des Serverzustands.
- `amicloud-worker` = normales CLI-Programm (voller vbcc-Startup), linkt Client-Backend:
  `cmd.c, http_amiga.c, cJSON.c, base64.c, md5.c, config.c, ui_hooks.c`.
  **Bibliotheken genau einmal**: bsdsocket + AmiSSL öffnet `http_init()` einmalig; alle HTTP-Calls
  bleiben strukturell im Worker-Prozess (http_amiga.c ist nicht threadsafe — passt).

### Worker-Start (in der Startup-Phase, vor dn_Task — dos.library noch erlaubt)
1. `FindPort("AmiCloud-Worker")` — existiert schon (manuell gestartet)? → übernehmen.
2. Sonst `SystemTags("L:amicloud-worker", SYS_Asynch TRUE, SYS_Input/Output NIL:,
   NP_StackSize 65536, …)` und `FindPort`-Poll mit `Delay(10)`, Timeout ~30 s
   → Mount schlägt sauber fehl (DOSFALSE + Fehler, Volume-Node entfernen).
3. README: Alternative `run >NIL: L:amicloud-worker` in der User-Startup.
4. Boot-Reihenfolge-Falle (Roadshow noch nicht oben): Worker macht `http_init()` **lazy beim
   ersten Request mit Retry** statt beim Start — nimmt dem `Activate=1`-Mount den Schrecken.

### Protokoll (`proto.h`)
`WREQ_PING, LIST, GET, PUT, DEL, MKDIR, RENAME, INFO, SETCOMMENT, SETPROTECT, SETDATE, QUIT`.
`WorkerMsg { struct Message; cmd; path[]; path2[]; UBYTE *buf; ULONG buflen; mtime;
protection; comment[80]; LONG result /* fertiger DOS-Fehlercode */ }`
- Synchron: ein wiederverwendbares Message-Struct + privater Reply-Port (CreateMsgPort in der
  Startup-Phase); `send_wreq()` = PutMsg/WaitPort/GetMsg. Volume blockiert währenddessen — v1 ok.
- **Puffer-Eigentum**: PUT: Handler AllocVec't, Worker liest nur, Handler FreeVec't nach Reply.
  GET/LIST: Worker AllocVec't Ergebnis (`WEntry[]` bzw. Dateiinhalt), Handler FreeVec't.
- Fehler-Mapping im Worker (eine Tabelle in proto.h): 404→ERROR_OBJECT_NOT_FOUND(205),
  401/403→213/223, 409-exists→ERROR_OBJECT_EXISTS(203), 409-nonempty→216,
  422/Upload-Fehler→ERROR_DISK_FULL(221), Transportfehler: READ→ERROR_SEEK_ERROR(219),
  WRITE/END→221, sonst→ERROR_INVALID_LOCK(211).

### Worker-Ablauf (worker.c)
`config_load("ENV:amicloud.cfg")` → stille `ui_set_hooks()` (notify→Log, confirm→ja,
progress→weiter), `ac_errors_to_stderr=0`, optional `http_log_path` aus `cfg.debug` →
`http_init()` + `http_warmup(cfg.server, cfg.verify_ssl)` (lazy, s. o.) →
`CreatePort("AmiCloud-Worker")` → Loop: GetMsg → Dispatch → ReplyMsg. QUIT → RemPort,
`http_cleanup()`, Ende.
Dispatch ≈ 1:1 auf Client-Funktionen: LIST→`cmd_list_raw`, DEL→`cmd_del`,
MKDIR→`cmd_mkdir_quiet`, RENAME→`cmd_rename`, INFO→`cmd_info_raw`, SETDATE→`cmd_setdate_mtime`.

### Neue Client-Funktionen (client/cmd.c/.h — in Commit 2 enthalten)
- `cmd_get_buf(cfg, remote, char **out, long *out_len)` — Download in Speicher
  (http_request liefert Body ohnehin im RAM), MD5 gegen `X-AmiCloud-MD5` prüfen.
- `cmd_put_buf(cfg, remote, buf, len)` — MD5 berechnen, Upload als Raw-Body mit `&md5=`.

### Handler-Integration (backend.c + vol.c)
- **Dir-Cache mit TTL ~10 s** (`cache_time` am CNode; Zeitquelle: Worker liefert „jetzt“ in jedem
  LIST-Reply mit — keine Uhr-Syscalls im Packet-Loop): LOCATE/EXAMINE/EXNEXT auf veraltetem
  Verzeichnis → WREQ_LIST, Merge (neu/aktualisiert/verschwunden; gelockte/geöffnete Knoten
  bleiben). Mutationen (PUT/DEL/MKDIR/RENAME) aktualisieren den Baum sofort + invalidieren Parent.
- **Lesen**: beim Öffnen (FINDINPUT/FINDUPDATE auf existierende Datei) ganze Datei per WREQ_GET
  in AllocVec-Puffer am OpenFile; READs/SEEKs aus dem Puffer. Datei > freies RAM → Fehler
  (README; Range-API als dokumentierte Zukunftsoption).
- **Schreiben**: FINDOUTPUT legt Knoten + Wachstumspuffer an; WRITE in den Puffer
  (`ERROR_NO_FREE_STORE` bei Alloc-Fehler); **ACTION_END mit dirty → WREQ_PUT der ganzen Datei**;
  Upload-Fehler → END mit DOSFALSE + Fehlercode, Knoten als stale markieren.
- ACTION_DIE bleibt DOSFALSE (Unmount bräuchte dos.library); Worker-Stopp nur manuell/Reboot — README.

### Test Stufe 2
Erst CLI-Client gegen Server prüfen (`amicloud list /`) → Worker manuell starten, mounten,
`Dir CLOUD:` zeigt Server-Inhalt → komplette Stufe-1-Batterie gegen den Server, Ergebnisse in
web.php/Shell gegenprüfen → 1-MB-Datei rein/raus + Vergleich → `List` 2× in 10 s (2. Mal
instant = Cache) → Netz ziehen mitten im Copy (Fehler im Shell, Handler überlebt, nächstes Dir
erholt sich) → Auto-Spawn ohne manuellen Worker testen.

---

## Stufe 3 — Voller Funktionsumfang (Commit 3, ohne Sync)

### Client-Lücken schließen (client/cmd.c/.h)
- `cmd_setcomment(cfg, path, comment)` → `action=setcomment&comment=` (URL-encoded, ≤79).
- `cmd_setprotect(cfg, path, bits)` → `action=setprotect&bits=` — Server speichert **rohe
  Amiga-Bits**, `fib_Protection` unverändert durchreichen (RWED active-low, nirgends invertieren).
- `AcEntry` um `long protection; char comment[80];` erweitern + in `cmd_list_raw()` parsen
  (Server-JSON hat beides schon; Parsing von `cmd_info_raw` kopieren). Additive Felder —
  shell/panel/gui kompilieren weiter.

### Handler-Metadaten
- SET_PROTECT/SET_COMMENT/SET_DATE → WREQ_SETPROTECT/SETCOMMENT/SETDATE → Server; Cache-Knoten
  aktualisieren. Kommentar >79 → `ERROR_COMMENT_TOO_BIG`(215) handler-seitig.
- EXAMINE/EXNEXT füllen jetzt `fib_Protection` + `fib_Comment` (BCPL!) aus dem Cache —
  `List CLOUD:` zeigt alles.
- **Datumskonvertierung im Worker** (der hat die Config): DateStamp ↔ UTC-Epoch mit
  Amiga-Epoche-Offset 252460800 (1978→1970) und `gmt_offset`/`dst` aus ENV:amicloud.cfg —
  Konvention von `client/localfs_amiga.c` übernehmen/spiegeln.

### Restpunkte
- **Rename = Move** über Verzeichnisse (Server kann das; Ziel-Parent muss existieren,
  409 → ERROR_OBJECT_EXISTS). Reiner Case-Change-Rename testen; falls Server no-op →
  als Limitation dokumentieren.
- **Kein rekursives Delete im Handler**: AmigaDOS `Delete … ALL` läuft selbst per ExNext +
  einzelne DELETE_OBJECTs; nicht-leeres Dir → ERROR_DIRECTORY_NOT_EMPTY reicht.
  `cmd_del_recursive` bleibt Shell-Feature. (README.)
- **DISK_INFO** mit plausiblen Zahlen (32-bit-Überlauf beachten): BytesPerBlock=512,
  NumBlocks=0x003FFFFF (~2 GB), Used=½, ID_DOS_DISK, ID_VALIDATED, id_VolumeNode, id_InUse.
- FINDUPDATE + SET_FILE_SIZE-Kanten (kürzen, dann Close → kürzerer Upload).

### Test Stufe 3
`FileNote`/`Protect`/`SetDate` → `List` + web.php gegenprüfen, Remount-Persistenz (kommt vom
Server, nicht aus RAM) · `Copy CLONE` erhält Schutzbits · SetDate respektiert gmt_offset/dst ·
Cross-Dir-Move + Rename-auf-Existierendes (Requester „object exists“) · `Delete … ALL` +
Workbench-Schublade löschen · `Info CLOUD:` / Workbench-Fenstertitel „% full“ plausibel ·
volle Regression Stufe 1+2.

---

## Risiko-Checkliste
1. BCPL überall: Locks = BPTR (BADDR/MKBADDR), Namen/Kommentare = BSTR, FIB-Strings längenpräfixiert — alles nur über util.c.
2. Handler `-nostdlib` + eigener Entry; Worker bewusst mit normalem Startup via SystemTags.
3. Nach `dn_Task`: exec-only im Handler; Replies per PutMsg; auch Delay() verboten.
4. Pfade relativ zum Lock: `sub/dir/file`, führendes `:`, leere Komponente = Parent.
5. ExNext-Keys = seq-Nummern, nie Pointer.
6. Puffer-Eigentum über Prozessgrenze: AllocVec/FreeVec, ein Owner pro Richtung.
7. Server-Upload = Ganzdatei → Puffern + Upload bei END; Lesen ganze Datei bei Open (RAM-Limit dokumentiert).
8. Case-Insensitivität = Latin-1.
9. Stack: Handler 16 KB (Mountlist), Worker 64 KB (AmiSSL).

## Verifikation gesamt
Build erfolgt **nativ auf dem Amiga** (VBCC, wie client/Makefile.amiga — hier kein Amiga-Toolchain
verfügbar): Ich schreibe Code + Makefile + README-Testanleitungen; der Nutzer baut und testet
jede Stufe auf echter Hardware anhand der Checklisten oben, bevor die nächste Stufe beginnt.
Auf dem Mac prüfbar: Syntax-Review, Server-API-Abgleich (`scripts/test_api.sh`-Umgebung:
MariaDB + `php -S 127.0.0.1:8180 -t server/`).

## Commits
1. `cloudhandler/` Stufe 1 (RAM-POC) + Mount-Beispiel + README.
2. Stufe 2: proto.h, worker.c, backend.c/h, vol.c-Cache, Makefile, `client/cmd.{c,h}` (+get_buf/put_buf), README.
3. Stufe 3: Metadaten-Pfade, `client/cmd.{c,h}` (setcomment/setprotect/AcEntry), README final.
