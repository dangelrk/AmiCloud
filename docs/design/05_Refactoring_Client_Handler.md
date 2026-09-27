# 05 — Refactoring Client + Handler auf Bibliotheks-Architektur

## Kontext

Der AmiCloud-Code teilt sich heute über **flache Dateien in `client/`** (die drei Apps
`amicloud`, `amicloud-shell`, `amicloud-commander` sowie zwei Handler-Bäume) denselben Kern
nur per Copy-in-den-Makefiles (`COMMON_SRCS`). Das ist fragil und blockiert die in
`docs/01–03` beschriebene Roadmap.

Ziele dieses Umbaus:

1. **Sync raus aus den interaktiven Apps.** Die Engine wird nicht gelöscht, sondern nach der
   Migrationsregel („Code nicht neu schreiben, nur verschieben", `docs/03`) in eine eigene
   Bibliothek `libcloudsync` ausgelagert. Die UI-Anbindungen (Shell-Kommando, je ein
   Commander-Menü) werden entfernt.
2. **Voller Bibliotheks-Umbau bis `libcloudfs`.** `libcloudcore`, `libcloudhttp`,
   `libcloudfs`, `libcloudsync` extrahieren; alle vier Apps **und** der Handler-Worker ziehen
   ihren Kern aus denselben Bibliotheken (`docs/02`).
3. **Neue `src/`-Ordnerstruktur.** Alles Neue landet in sauberen `include/`+`src/`-Bäumen. Das
   **alte `handler/` bleibt unangetastet** als Referenz; der aktuelle `cloudhandler/` wird nach
   `handler/cloudhandler/` mit `src/` überführt.

Zusätzlich bindend:

- **ADR `docs/04_ADR_Multiuser_Architektur.md`** — v1 bleibt funktional Einbenutzer, aber die
  Bibliotheks-Grenzen müssen Mehrbenutzer *vorbereiten* (siehe Abschnitt „Multiuser-Vorbereitung").
- **Modul-Kapselung** — *Kein Modul darf Wissen besitzen, das in ein anderes Modul gehört*
  (siehe Abschnitt „Verbindliche Regel").

Ergebnis: eine Zielstruktur nach `docs/02`, in der Commander, Shell, CLI, Handler und der
künftige Sync-Dienst denselben Kern nutzen.

---

## Ist-Zustand (verifiziert)

- **Gemeinsamer Kern** (`COMMON_SRCS` in beiden Makefiles): `cmd.c http_{pc,amiga}.c base64.c
  config.c cJSON.c md5.c ui_hooks.c`.
- **Drei Apps**, alle über denselben Kern:
  - `amicloud` — CLI, `main.c`.
  - `amicloud-shell` — `shell_main.c shell.c panel.c localfs_* history lineedit_* pattern_*`.
  - `amicloud-commander` — zwei Frontends: `client/gui_amiga/` (MUI) und `client/gui_qt/`
    (Qt6, linkt dieselben `.c`-Dateien).
- **Sync-Engine liegt isoliert** in `client/panel.c` Z. **566–952** (`SyncEntry`, `SyncIndex`,
  `SyncAction*`, `index_*_tree_sync`, `queue_transfer_by_mtime`, `panel_sync()`), Interface
  `panel.h` Z. 156–172. Anbindungen:
  - `shell.c` — `do_sync`, Dispatch Z. 823, Hilfetext Z. 259.
  - `gui_amiga/gui.c` — `ID_SYNC`, `do_sync_menu`, Menü Z. 757, Case Z. 951.
  - `gui_qt` — `mainwindow::doSync` + Menü, `transferjob` `SyncPanels`-Zweig + `syncFinished`.
- **Support-Funktionen** von Sync (`cmd_setdate_mtime`, `localfs_utime`, `AcConfig.gmt_offset/dst`,
  UTC-Epoch-Konvertierung) werden **auch vom Handler** (Stufe 3, `WREQ_SETDATE`) genutzt →
  bleiben erhalten, wandern nur in die passende Lib.
- **Handler**: zwei Bäume. `cloudhandler/` (neu, Stufe 1–3, Worker linkt `../client/cmd.c`) und
  `handler/` (alt, WebDAV-Variante). Der Handler-Prozess ist `-nostdlib`/exec-only; nur der
  **Worker** linkt Client-Kern.

---

## Zielstruktur (nach `docs/02`, mit `src/`-Layout)

```
libs/
  libcloudcore/   include/cloudcore/*.h   src/   (Makefile.pc/.amiga)
  libcloudhttp/   include/cloudhttp/*.h   src/
  libcloudfs/     include/cloudfs/*.h     src/
  libcloudsync/   include/cloudsync/*.h   src/
client/
  common/                 include/ src/   panel.c/.h ohne Sync (Dual-Pane + Copy-Engine)
  amicloud/               src/            main.c
  amicloud-shell/         src/            shell_main.c shell.c history.c lineedit_*
  amicloud-commander-mui/ src/            (aus gui_amiga/)
  amicloud-commander-qt/  src/            (aus gui_qt/) + CMakeLists.txt
  cloudsync/              src/            Platzhalter-Commodity (Phase 5), linkt libcloudsync
handler/
  cloudhandler/   src/ mount/            (aus cloudhandler/); Worker linkt die Libs
  handler/  -> BLEIBT UNVERAENDERT (Referenz, altes WebDAV)
server/
  php/  -> keine funktionale Aenderung noetig (Begruendung unten)
```

Jede Lib baut eine **statische Bibliothek** (`libcloudX.a` / Amiga `libcloudX.lib`); Apps linken
diese über `-Ilibs/libcloudX/include -Lpfad -lcloudX`. Ein Top-Level `Makefile.pc` orchestriert.

### Bibliotheks-Zuordnung

| Lib | Quellen (aus `client/`) | Öffentliche Typen/API | Abhängig von |
|-----|-------------------------|-----------------------|--------------|
| **libcloudcore** | `config.c` `base64.c` `cJSON.c` `md5.c` `ui_hooks.c` + Typen aus `amicloud.h`/`cmd.h` + DateStamp↔Epoch/gmt-dst-Util | `AcConfig`, `AcEntry`, `AcInfo`, `ListMode`, `ui_*`, config/base64/md5/json, **eine** Zeit-Konvertierung | — |
| **libcloudhttp** | `http.h` `http_pc.c` `http_amiga.c` `cmd.c/.h` | `http_*`, alle `cmd_*` (list/get/put/del/info/mkdir/rename/setcomment/setprotect/setdate, `*_buf`, `*_raw`, `del_recursive`) | core |
| **libcloudfs** | `localfs.h` `localfs_pc.c` `localfs_amiga.c` `localfs_util.c` `pattern.*` | `localfs_*` (Scan, mtime, `utime`, Größe), `pattern_*` | core |
| **libcloudsync** | Sync-Block aus `panel.c` (Z. 566–952) + `panel.h` 156–172 → `sync.c`/`sync.h` | `cloudsync_run()` (= ex-`panel_sync`), **`SyncSummary`, `SyncAction*`, Konfliktregeln** (hier beheimatet, nicht in core) | core, http, fs |

**Nicht als Lib:** `core/common/panel.{c,h}` (Dual-Pane-Modell + `panel_copy_file`-Transfer-
Engine) bleibt geteilter Frontend-Code von Shell + beiden Commandern — UI-Modell, kein sauberer
Lib-Kandidat. **Nutzt nur öffentliche Lib-APIs** (core/http/fs) — kein http/fs-Internwissen.

---

## Verbindliche Regel: Modul-Kapselung

**Kein Modul besitzt Wissen, das in ein anderes gehört.** Jedes Modul exponiert nur seine
öffentliche Schnittstelle; niemand greift auf Interna oder Domänenwissen eines anderen zu.
Konkrete Konsequenzen (Prüfpunkte beim Umbau):

- **Server-Vertrag** (Auth-Verfahren, URL/Endpoint-Aufbau, HTTP↔DOS-Fehler-Mapping): **nur
  `libcloudhttp`**. Apps/Handler/Sync kennen nur `cmd_*`-Ergebnisse, nie URLs oder Auth-Details.
- **Zeit-/Datumswissen** (Amiga-Epoche-Offset, `gmt_offset`/`dst`, DateStamp↔UTC-Epoch): **genau
  ein Ort** (Zeit-Util in `libcloudcore`). Die heutige Duplizierung in `client/localfs_amiga.c`
  **und** `cloudhandler/worker.c` (`epoch_to_datestamp`) wird auf diese eine Quelle
  zusammengeführt; Worker/localfs rufen nur noch.
- **Sync-Domäne** (`SyncSummary`, `SyncAction*`, Index-/Konflikt-/mtime-Entscheidung): **nur
  `libcloudsync`** — nichts davon in core oder panel.
- **Lokale FS-Semantik** (Scan, Pattern, mtime, `utime`): **nur `libcloudfs`**.
- **Gerichtete, azyklische Abhängigkeiten:** core ← {http, fs} ← sync ← Apps. Keine
  Rückwärts-Includes, keine App-Typen in Libs.

---

## Milestones (jeder Schritt baut & testet grün, bevor der nächste beginnt)

### M1 — Sync entfernen + `libcloudsync` anlegen  *(liefert den expliziten Wunsch zuerst)*
- UI-Anbindungen entfernen: `shell.c` (`do_sync`, Dispatch Z. 823, Hilfetext Z. 259);
  `gui_amiga/gui.c` (`ID_SYNC`, `do_sync_menu`, Menüeintrag Z. 757, Case Z. 951); `gui_qt`
  (`mainwindow::doSync` + Menü, `transferjob` `SyncPanels`-Zweig, `syncFinished`, `m_sync*`-Member,
  Header-Deklarationen).
- Sync-Engine aus `panel.c` Z. 566–952 herausschneiden → `core/libcloudsync/src/sync.c` +
  `include/cloudsync/sync.h` (Funktion `panel_sync` → `cloudsync_run`, sonst 1:1 übernehmen).
  `SyncSummary`/`SyncAction*` **in `libcloudsync`** beheimatet (Sync-Domäne, nicht core).
- `panel.c/.h` behalten nur Dual-Pane + Copy-Engine.
- **Support bleibt** (`cmd_setdate_mtime`, `localfs_utime`, `gmt_offset/dst`) — wandert später in
  http/fs/core, wird von `libcloudsync` + Handler weiter genutzt.
- **Verify:** alle Apps bauen ohne Sync-Kommando/-Menü; `libcloudsync` baut (vorerst ohne
  App-Consumer, dormant bis Phase-5-`cloudsync`).

### M2 — `libcloudcore`
- Dateien nach `core/libcloudcore/{src,include/cloudcore}` verschieben; gemeinsame Typen
  (`AcConfig` aus `amicloud.h`; `AcEntry/AcInfo/ListMode` aus `cmd.h`) in
  `include/cloudcore/types.h` konsolidieren. `cmd.h` inkludiert diese. **Eine** Zeit-Util
  (DateStamp↔Epoch, gmt/dst) hier ansiedeln.
- `Makefile.pc`/`Makefile.amiga` der Lib (baut `libcloudcore.a`).
- Alle Consumer-Includes + Makefiles/CMake auf neue Pfade zeigen.
- **Verify:** CLI, Shell, beide Commander, Handler-Worker bauen weiter.

### M3 — `libcloudhttp` (inkl. `cmd.c`)
- `http.*`, `cmd.*` nach `core/libcloudhttp/{src,include/cloudhttp}`.
- **Handler-Worker repointen:** `cloudhandler/worker.c` linkt statt `../client/cmd.c` künftig
  `libcloudhttp` + `libcloudcore` (`-Ilibs/.../include -lcloudhttp -lcloudcore`).
- **ADR-Leitplanke (kein v1-Feature):** Auth **und** URL/Endpoint-Aufbau bleiben die einzigen
  zwei Punkte, die das Server-Vertragsdetail kennen — Auth zentral in `http_request(...,auth)`,
  URL-Bau ausschließlich in `cmd.c` (`cmd_resolve_path`/URL-Builder). So sind der spätere Wechsel
  von Basic-Auth auf Session/Token und von `api.php?action=` auf eine benutzerneutrale REST-API
  (`GET /files`) je eine lokale Änderung in dieser Lib. Apps/Handler kennen nur „authentifiziert",
  nie eine User-ID.
- **Verify:** wie M2 + Handler-Worker linkt gegen die Libs.

### M4 — `libcloudfs`
- `localfs_*`, `localfs_util`, `pattern_*` nach `core/libcloudfs/{src,include/cloudfs}`.
- `libcloudsync` linkt jetzt fs+http (statt vormals `panel.c`-lokaler Aufrufe).
- **Verify:** Shell/Commander (nutzen fs) + `libcloudsync` bauen.

### M5 — Apps in `src/`-Layout + Build-Neuverdrahtung
- `main.c` → `client/amicloud/src`; `shell_main.c shell.c history.c lineedit_*` →
  `client/amicloud-shell/src`; `gui_amiga/*` → `client/amicloud-commander-mui/src`; `gui_qt/*` →
  `client/amicloud-commander-qt/src`; `panel.*` → `core/common`.
- Neue Build-Dateien: pro App `Makefile.pc`/`Makefile.amiga`, Qt `CMakeLists.txt` (referenziert
  Lib-`src`/`include` statt flacher `../*.c`), `Makefile.win`, Top-Level-`Makefile.pc`.
- `client/cloudsync/` als **Platzhalter** anlegen (kleines `main`, das `libcloudsync` linkt) —
  Roadmap-Phase 5, hier nur Gerüst.
- **Verify:** Voll-Build aller Targets auf PC (`make -f Makefile.pc`) + Qt-Build.

### M6 — `handler/cloudhandler/src`
- `cloudhandler/*` → `handler/cloudhandler/{src,mount}`; Handler-Prozess-Code (`handler.c actions.c
  vol.c util.c` — exec-only, `-nostdlib`) bleibt handler-lokal; nur `worker.c` linkt die Libs (aus
  M3/M4). `Makefile.amiga` anpassen.
- **Altes `handler/` bleibt unverändert.**
- **Verify:** `cloudhandler`-Build (nativ am Amiga; hier nur Syntax-/Include-Abgleich gegen
  `Include_H/`, `rkrm/`).

---

## Multiuser-Vorbereitung (ADR `docs/04`)

V1 bleibt Einbenutzer; Mehrbenutzer wird **nur an den Lib-Grenzen vorbereitet**, nicht
implementiert:

- **Auth als isolierte Schicht** (siehe M3): ein Choke-Point in `libcloudhttp`; Basic-Auth jetzt,
  Session/Token später ohne App- oder Handler-Änderung. Clients übergeben **nie** eine User-ID bei
  Dateioperationen — der Server entscheidet per Session/Auth über den Datenbestand.
- **Benutzerneutrale API-Kapselung:** aller Endpoint-/URL-Aufbau nur in `cmd.c` → spätere
  Umstellung auf `GET /files`, `POST /upload` etc. ist lokal.
- **„Client verwaltet Synchronisation":** bestätigt die Lib-Grenzen — Sync-Logik gehört auf den
  Client (`libcloudsync` + künftige `cloudsync`-Commodity), der Server verwaltet nur Daten/Auth.
  Kein Sync-Code im Server.
- **Erweiterbares Datenmodell:** ADR-Felder (`owner_id`, `revision`, `upload_time`) sind
  serverseitig; clientseitig nur relevant, dass `AcEntry` **additiv** erweiterbar bleibt (wie schon
  bei `protection`/`comment`) — keine Struktur-Brüche.
- **Nicht Teil dieses Umbaus** (ADR „Nicht Bestandteil von v1"): Freigaben, Gruppen, ACLs,
  öffentliche Links, Quotas, `install.php`, `users/sessions/…`-Tabellen — reine Server-Zukunft.

---

## Server

**Keine funktionale Änderung nötig.** Sync-Entfernung ist rein clientseitig. Der `setdate`-Endpoint
(`api.php`) bleibt, weil ihn der Handler (Stufe 3, `WREQ_SETDATE`) und `cmd_setdate_mtime` weiter
brauchen — er war nur *auch* von Sync genutzt. Optional (separat): `server/*` nach `server/php/`
gemäß `docs/02` verschieben — reine Ordnerkosmetik, kein Code.

---

## Verifikation (End-to-End)

- **PC/macOS** (hier ausführbar): nach jedem Milestone `make -f Makefile.pc` (CLI+Shell) +
  Qt-`cmake --build`; Smoke-Test gegen lokalen Server (`php -S 127.0.0.1:8180 -t server/`, vgl.
  `scripts/test_api.sh`): `amicloud list /`, Shell `get/put/ls/info`, Commander-Copy. Prüfen, dass
  **kein** `sync`-Kommando/-Menü mehr existiert und alle vier Apps + `libcloudsync` linken.
- **Amiga** (nur nativ, hier kein Toolchain): pro Lib/App/Handler `Makefile.amiga`-Build durch den
  Nutzer; Handler-Batterie aus `docs/handler-plan.md`. Hier: Syntax-Review + Abgleich der
  Includes/Typen gegen `Include_H/`, `rkrm/`, `netinclude/`.
- **Regression:** vorhandene Funktionalität (Copy, wildcard get/put, rekursiv, MD5/Info, Metadaten)
  unverändert; nur Sync-UI verschwindet.

---

## Reihenfolge & Abhängigkeiten (Kurzfassung)

```
M1  Sync raus + libcloudsync         (Quick Win, liefert den Kernwunsch)
M2  libcloudcore                     (Fundament: Typen, Utils, EINE Zeit-Konvertierung)
M3  libcloudhttp (+ cmd)             (Handler-Worker auf Libs umstellen; Auth/URL-Choke-Point)
M4  libcloudfs                       (libcloudsync bekommt fs/http-Unterbau)
M5  Apps -> src/-Layout + Builds     (Voll-Build PC + Qt)
M6  handler/cloudhandler/src         (altes handler/ bleibt)
```
