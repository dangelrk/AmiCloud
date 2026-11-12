# Dateinamenlänge: klassisches FFS/OFS (30 Zeichen)

Stand 12.11.2026 – umgesetzt in AmiCloudCore, AmiCloud, AmiOwnCloud,
AmiVCS und AmiDevIDE (noch nicht veröffentlicht).

## Problem

FFS und OFS speichern höchstens 30 Zeichen pro Name und kürzen längere
**ohne Fehler** – beim Anlegen und beim Öffnen. Zwei Namen mit gleichem
Anfang werden zu einer Datei (im Emulator auf `DOS\3` nachgestellt: die
zweite überschreibt die erste). Dateisysteme mit langen Namen (FFS mit
`DOS\6`/`DOS\7` ab OS 3.2, SFS, PFS3) erkennt `localfs_name_max()` per
Probedatei.

## Was geändert wurde

**A – Fehler**
- AmiVCS `gc`: Packs heißen `<24hex>.pack`/`.idx` (29/28 Zeichen). Vorher
  wurden `pack-<32hex>.pack` und `.idx` auf FFS derselbe Name – der Index
  ersetzte das Pack, danach wurden die Einzelobjekte gelöscht: das Repo war
  verloren. Zusätzlich prüft `gc` vor dem Löschen, ob Pack und Index mit den
  geschriebenen Größen auf der Platte stehen.
- AmigaIDE: Editor-Daten in `<name>.auser` statt `<name>.aproj.user`
  (überschrieb bei 24 Zeichen Projektnamen die Projektdatei).
- AmiVCS-Merge: `<pfad>.theirs` wird gekürzt, wenn es nicht passt
  (`vcs_theirs_rel()`, deterministisch aus Pfad und Namenslänge) – vorher
  überschrieb es bei 30-Zeichen-Namen die Arbeitsdatei. Abweichung vom
  ursprünglichen Plan: kein `MERGE_THEIRS`, der Name wird aus Pfad und
  Namenslänge des Volumes berechnet (kein neues Dateiformat).
- `get`, Shell `get`/`get -r`/`mget`, Commander: Ziel wird vorher geprüft
  (`localfs_name_fits()`, `panel_get_target_fits()`), zu lange Namen werden
  übersprungen; vorher wurde eine andere Datei überschrieben bzw. bei einem
  Fehler gelöscht.
- Handler (AMICLOUD:, OWNCLOUD:): Namen über 106 Zeichen werden nicht mehr
  still gekürzt (neue: `ERROR_INVALID_COMPONENT_NAME`, Servernamen: nicht
  gelistet).
- Nicht umgesetzt: der Sync-Probelauf (`-n`) rechnet weiter mit 255 statt
  mit der Grenze des Laufwerks (die Probe schreibt eine Datei, das darf ein
  Probelauf nicht). Er zeigt also auf FFS nicht an, was der echte Lauf
  wegen zu langer Namen überspringt.

**B – Amiga-Modus als Standard**
- AmiCloud-Server, pro Konto (`users.long_names`, `migrate_007.sql`): neue
  Namen höchstens 30 Zeichen (upload neuer Dateien/Ordner, mkdir, rename auf
  neuen Namen; API und Web). `status.name_max` = 30 bzw. 255. Schalter:
  Weboberfläche *Persönliche Einstellungen → Dateinamen*,
  `amicloud-admin.php user:longnames`.
- libcloudsync: `SyncTransport.name_max` (aus demselben `status`-Aufruf wie
  `global_rev`) bzw. `SyncOptions.remote_name_max` – neue lokale Namen über
  der Grenze werden weder hochgeladen noch gelöscht, nur gemeldet.
- AmiVCS, pro Projekt: `.amivcsproject` (versioniert, `longnames=1`). Ohne
  sie lehnt `commit` neue Namen über 30 Zeichen, mit `:` oder nur in
  Groß/Klein verschieden ab. `amivcs config longnames on|off`, Häkchen in
  AmiVCSConfig und amivcs-gui (Qt).
- AmigaIDE: liest/schreibt dieselbe `.amivcsproject`; ohne lange Namen
  Projektname ≤ 24, Dateien/Ordner/Binary ≤ 30 (Qt und MUI).

**C – Auschecken**: clone, switch/checkout, pull, merge prüfen vor dem
ersten Schreiben die Namenslänge des Arbeitsverzeichnisses
(`vcs_names_fit_flat()`) und brechen sonst ohne Änderung ab.

**D – Doku**: Abschnitt „Lange Dateinamen“ in allen vier Handbüchern
(Guide + HTML), API.md, INSTALL.md.

## Upgrade-Hinweise (für die Release-Notes)

- **Server:** `migrate_007.sql` als MariaDB-root einspielen (Prüfung vorher:
  `SHOW COLUMNS FROM users LIKE 'long_names';`). Konten mit vorhandenen
  Namen über 30 Zeichen behalten lange Namen, alle anderen kommen in den
  Amiga-Modus. Ohne Migration verhält sich der neue Server wie der alte.
- **Alte Clients mit neuem Server:** funktionieren; neue lange Namen im
  Amiga-Modus enden mit HTTP 400. **Neue Clients mit altem Server:** keine
  Grenze (kein `name_max`).
- **AmiVCS:** `gc` älterer Versionen auf FFS nicht mehr benutzen (zerstört
  das Repository). Repos ohne `.amivcsproject` sind im Amiga-Modus – neue
  lange Namen lehnt `commit` ab; vorhandene bleiben. Alte amivcs-Versionen
  ignorieren `.amivcsproject`. Alte und neue Pack-Namen werden von alten wie
  neuen Versionen gelesen.
- **AmigaIDE:** `.aproj.user` wird noch gelesen und nicht gelöscht; neu
  geschrieben wird `.auser`.

## Prüfung

- Unit: libcloudsync 126 (u. a. Amiga-Modus, Grenze aus dem Status),
  libcloudvcs, IDE ctest 20/20 (u. a. `legacyUserFile`, `amigaNames`).
- CLI: AmiVCS `test_cli.sh` 103 (gc mit altem+neuem Pack, Längenprüfung mit
  `AMICLOUD_NAME_MAX=30`, gekürzte `.theirs`, Amiga-Modus beim Commit).
- Server: `make server-test` 48 + 34 (7 neue Amiga-Modus-Fälle);
  Migration auf alter Datenbank mit Bestandskonten, PHP ohne Spalte.
- Emulator (FFS `DOS\3` auf RAD:): alter `gc` zerstört, neuer nicht;
  `amicloud get` alt überschreibt, neu überspringt; `amicloud-sync` von
  LNFS lädt im Amiga-Modus nicht hoch; `amivcs switch` auf FFS lehnt ab;
  AmiVCSConfig-Häkchen.
- Nicht im Emulator getestet: Handler über 106 Zeichen, MUI-Dialoge der
  IDE und des Commanders (nur cross-kompiliert).
