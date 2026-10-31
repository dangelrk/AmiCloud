# Plan: AmiCloud Desktop – Ordnerabgleich für macOS, Windows, Linux

Stand: 31.10.2026 · Status: Entwurf, Fassung 3 (Entscheidungen in § 11 eingearbeitet, keine Migration nötig). Der Entwurf wurde von einem
Subagenten gegen den Code geprüft und die Befunde eingearbeitet. Umgesetzt
ist noch nichts.

## 1. Ziel

Ein Desktop-Client für AmiCloud, der wie der ownCloud-Client im Hintergrund
läuft und einen lokalen Ordner mit dem AmiCloud-Server abgleicht.

- **Ein Konto** (Server, Benutzer, App-Passwort).
- **Ordnerwahl:** Ein lokaler Ordner spiegelt den Server. Welche
  Server-Ordner dazugehören, wählt man in einem Baum per Häkchen.
- **Fallback:** genau ein Server-Ordner (auch die Wurzel `/`). Das
  Datenmodell bleibt für die Auswahl offen (§ 6.3).
- **Plattform-Reihenfolge:** zuerst macOS, dann Windows, dann Linux.
- **Gleiches Verhalten überall:**
  - Datensicherheit, Konfliktbehandlung und Schutz vor Massenlöschung
    kommen in den gemeinsamen Sync-Kern `libcloudsync`.
  - Davon profitieren auch `amicloud-sync` (Amiga und PC),
    `amiowncloud-sync`, `AmiOwnCloudSync` und `amiowncloud-synctest`
    (WebDAV).

Vorerst nicht geplant sind:

- mehrere Konten,
- virtuelle Dateien bzw. Platzhalter,
- die Verwaltung von Freigaben,
- Overlay-Symbole im Explorer bzw. Finder.

### Keine Altlasten (entschieden 31.10.2026)

Der Abgleich ist noch nirgends im Einsatz. Deshalb gibt es **keine
Migration und keine Rücksicht auf Vorhandenes**:

- **Gespeicherter Stand:** Das Format von `.amicloudsync` darf sich frei
  ändern (Identität, Index, Zwischenspeicherung). Bekommt die Datei eine
  `format`-Nummer und liegt eine andere vor, wird sie verworfen.
- **Schnittstelle:** `cloudsync_run`/`cloudsync_run_t` werden durch
  `cloudsync_run_ex` **ersetzt**, keine Hüllen. Die vier Aufrufer werden
  angepasst.
- **Server:** keine Unterstützung für Server ohne die neuen
  Upload-Bedingungen. Die Clients setzen die neue Server-Version voraus.
  Schemaänderungen kommen trotzdem als `migrate_00x.sql` für die
  vorhandene Server-Installation, ohne Datenumbau.
- **Zeichensatz:** Es gibt keine Bestände mit Latin-1-Namen, die
  umgewandelt werden müssten.

## 2. Ausgangslage (aus dem Code, geprüft)

| Baustein | Stand | Fundstelle |
|---|---|---|
| Sync-Kern | 3-Wege-Abgleich gegen den gespeicherten Stand (`<lokal>/.amicloudsync`). Downloads laufen erst in `.amicloudsync.part`. Der Kern ist transportneutral (AmiCloud-API und WebDAV). Lokale MD5 wird nur neu berechnet, wenn sich Größe oder Zeit geändert haben. Zeittoleranz 2 s. | `sync.c`, `syncstate.c`, `transport.h` |
| Konflikte heute | Sie werden **übersprungen und gemeldet**. Beide Seiten bleiben unverändert. `had_conflicts` erzwingt bei jedem Lauf einen kompletten Neuabgleich, bis jemand den Konflikt von Hand löst. Ausnahme „Datei gegen Verzeichnis“: Dort fällt der Eintrag aus dem Stand, und die Kinder erzeugen bei jedem Lauf Fehler. | `sync.c:353-367, 500-501` |
| Konfliktfälle | Datei gegen Verzeichnis; beidseitig neu mit verschiedenem Inhalt; beidseitig verschieden geändert; entfernt gelöscht und lokal geändert; lokal gelöscht und entfernt geändert. **Bei WebDAV** fehlt die Server-MD5. Darum ist dort jede Datei, die beim ersten Abgleich auf beiden Seiten liegt, ein „Konflikt“, ebenso jede beidseitig gleiche Änderung. | `sync.c:516-524, 585` |
| Schnellweg | Ist `global_revision` unverändert, gilt der Stand als Server-Abbild. `change_seq` und `global_revision` sind **derselbe Zähler**. | `sync.c:460-468`, `FileStore.php:92-98` |
| Server | `changes?since=` liefert Änderungen samt Löschungen, MD5, Revision und ID. Es gibt **keine Paginierung**. Grabsteine werden nie aufgeräumt. **Umbenennungen erscheinen nicht als Löschung des alten Pfads.** Der Upload überschreibt ohne Revisionsbedingung. Pfade sind nur als UTF-8 erlaubt, `<>:"\|?*` ist verboten, und `\` wird **still zu `/`**. Löschen ist endgültig (`unlink`). Upload bis `MAX_UPLOAD` (100 MB), ganz im Speicher, ohne Wiederaufnahme. Basic-Auth mit App-Passwort oder Kontopasswort. Throttle: 5 Fehlversuche pro IP und Benutzer. | `FileStore.php:15, 31, 410-420, 500, 613-643, 875-893`, `api.php:72`, `Throttle.php:20-22` |
| Zeiten | `list` liefert echte Epoch-Werte. `changes` nutzt `UNIX_TIMESTAMP(modified_at)`, aber `setdate` schreibt Ortszeit in `AMICLOUD_TIMEZONE`, und die Datenbank-Verbindung setzt keine `time_zone`. Folge: möglicher Versatz und Mehrdeutigkeit bei der Sommerzeit. | `FileStore.php`, `DB.php` |
| Globale Zustände | `http_pc.c`: **ein einziges `g_curl`-Handle**, außerdem `g_timeout` und `g_last_error`. `cmd.c` und `webdavcmd.c`: `g_last_status`. `ui_hooks.c`: `g_hooks`, `g_break`. `progress.c`. `localtime()`. `AcConfig` wird per Zeiger übergeben und ist kein globaler Zustand. | `http_pc.c:164-167, 469` u. a. |
| Abbruch | `sync.c` fragt `ui_break_pending()` **nirgends** ab. Liefert `ui_progress()` 0, bricht nur die laufende Übertragung ab, alle weiteren Aktionen laufen weiter (auch Löschungen). | `sync.c:657-782`, `http_pc.c:108` |
| Windows | `amicloud.h` kennt `PLATFORM_WIN`. Ein Port fehlt aber: Direktes `fopen`/`remove`/`rename` steckt auch in `md5.c`, `syncstate.c`, `sync.c`, `cmd.c` und `http_pc.c`. `long` hat unter Windows 32 Bit, Dateien über 2 GB gehen kaputt. | `amicloud.h:13` |
| Zeichensatz | **Es gibt keine Umwandlung Latin-1 ↔ UTF-8.** `url_encode` schickt Amiga-Umlaute als `%E4`, der Server antwortet mit 400. UTF-8-Namen vom Server kommen auf dem Amiga als „Ã¤“ an. Betroffen sind die Kommandozeile, der Sync, **die Handler `AMICLOUD:`/`OWNCLOUD:`** und Kommentare. | `cmd.c:75`, `FileStore.php:31` |
| Qt-Basis | `amicloud-commander-qt`: Qt 6 Widgets, CMake, bindet die C-Quellen direkt ein, nutzt libcurl, hat einen Anmeldedialog. | `apps/amicloud-commander-qt/` |
| Tests | Keine Host-Unit-Tests für den Kern. Es gibt nur `amiowncloud-synctest` gegen einen echten Server. | – |

### 2.1 Bestehende Datenrisiken (unabhängig vom Desktop-Client, betreffen heute schon Amiga und PC)

1. **Fehler beim Auflisten werden verschluckt.** `index_remote_tree_sync`
   und `index_local_tree_sync` kehren bei einem `list`- oder
   `opendir`-Fehler still zurück (`sync.c:142, 204`).
   - Ein Netzaussetzer, ein Ctrl-C während des Scans auf dem Amiga oder
     eine verweigerte Berechtigung unter macOS machen einen ganzen
     Teilbaum zu „auf der anderen Seite gelöscht“.
   - Die nachfolgende Löschphase führt das dann aus.
2. **WebDAV-DELETE ist rekursiv** (`webdav.c:467-481`, RFC 4918 Depth
   infinity).
   - Der Kern setzt nicht-rekursives Löschen voraus (`sync.c:746-752`).
   - Wird ein Ordner lokal gelöscht und enthält er auf dem Server eine
     geänderte Datei, vernichtet AmiOwnCloud diese Datei.
3. **Der Stand kennt seine Gegenseite nicht.**
   - Wechselt man für denselben lokalen Ordner den Startordner, das Konto
     oder den Server, sieht alles wie gelöscht aus.
4. **Kein Abbruch zwischen den Phasen**, siehe „Abbruch“ in der Tabelle
   oben.
5. **Lokaler Wurzelordner fehlt** (z. B. ein ausgehängtes Laufwerk):
   - Der Stand liegt im Ordner, ist also auch weg.
   - `localfs_mkdir_p(local_root)` (`sync.c:442`) legt den Ordner neu an
     und lädt alles neu herunter.
   - Unter Linux landet das auf der Systemplatte, auf dem Amiga erscheint
     der Requester „Please insert volume“.
6. **Groß-/Kleinschreibung:**
   - Liegen `a.txt` und `A.txt` auf dem Server, überschreibt der Download
     auf dem Amiga, Mac und Windows die eine mit der anderen.
   - Der nächste Lauf lädt diesen Inhalt dann über die andere hoch.
7. **Wettläufe:**
   - Eine lokale Änderung zwischen Scan und Download wird überschrieben
     (`localfs_replace` ist `remove` + `rename`).
   - Eine Server-Änderung zwischen Scan und Upload wird ebenfalls
     überschrieben.
   - Die `global_revision` am Laufende wird neu abgefragt (`sync.c:803`).
     Fremde Änderungen während des Laufs können so übersehen werden.

**Deshalb kommt vor alles andere eine Phase „Datensicherheit des Kerns“
(§ 3).** Sie ist ein eigenes Release für alle Amiga-Programme, auch ohne
Desktop-Client.

## 3. Datensicherheit des Kerns (alle Clients)

| # | Maßnahme | Ort |
|---|---|---|
| S1 | **Scan-Fehler sind fatal.** Scheitert `list` oder `opendir` irgendwo, wird der Lauf abgebrochen, bevor auch nur eine Aktion läuft. Nur der Stand bleibt. Alternative für später: den Teilbaum als „unbekannt“ markieren und seinen Stand übernehmen. | `sync.c` |
| S2 | **Abbruch zwischen allen Phasen und Aktionen.** Nach jeder Aktion und vor der Löschphase wird `ui_break_pending()` geprüft. Ein abgebrochener Lauf löscht nie. | `sync.c` |
| S3 | **Identität im Stand.** Server-URL, Benutzer und `remote_root` werden gespeichert. Weichen sie ab, wird der Stand verworfen, und der Lauf gilt als Erstabgleich. Ein Erstabgleich löscht nie, er führt nur zusammen. | `syncstate.c` |
| S4 | **Wurzelordner nicht automatisch anlegen**, wenn der Aufrufer einen vorhandenen Abgleich erwartet. Neues Flag `SYNC_EXPECT_EXISTING`, das die Clients setzen, sobald sie schon einmal abgeglichen haben (in der eigenen Konfiguration gemerkt). | `sync.c`, Clients |
| S5 | **WebDAV:** Einen Elternordner nur löschen, wenn der Lauf alle bekannten Kinder selbst gelöscht hat. Sonst wird der Ordner stehen gelassen und gemeldet. | `sync.c` (Planung der Aktionen) |
| S6 | **Kollisionen bei Groß-/Kleinschreibung:** Namen, die sich nur in der Schreibung unterscheiden (Vergleich je nach Plattform), werden beide übersprungen und gemeldet. Das gilt für den Amiga, Mac und Windows; Linux kann beide behalten. | `sync.c` + Hook aus libcloudfs (`localfs_case_sensitive()`) |
| S7 | **Erneut prüfen vor dem Überschreiben:** Vor dem Ersetzen einer lokalen Datei Größe und Zeit erneut vergleichen. Hat sie sich seit dem Scan geändert, wird die Aktion übersprungen und beim nächsten Lauf neu bewertet. | `sync.c` |
| S8 | **Server-Bedingung beim Upload** (Server-Erweiterung): `upload&if_revision=<n>` bzw. `&create_only=1`. Bei Widerspruch antwortet der Server mit 409, der Kern behandelt das wie einen Konflikt (§ 4). Die neue Server-Version ist Voraussetzung. | `FileStore.php`, `cmd.c`, Transport |
| S9 | **`global_revision` vom Laufbeginn speichern**, nicht die vom Ende. Die eigenen Änderungen erkennt der nächste Lauf über die MD5 als „schon bekannt“. | `sync.c:803` |
| S10 | **`put()` liefert die Server-Metadaten zurück** (Revision, MD5, ID), statt `revision=1` zu raten (`sync.c:612`). Das verhindert unnötige Downloads und Schein-Konflikte. | `transport.h`, beide Transporte |
| S11 | **Stand regelmäßig zwischenspeichern** (alle N Aktionen bzw. M Sekunden). Ein Absturz nach Stunden verliert dann nicht alles. | `sync.c`, `syncstate.c` |
| S12 | **Stand-Index sortiert**, mit `bsearch` statt linearer Suche (`syncstate.c:145-180`). Sonst wächst der Aufwand mit O(n²). | `syncstate.c` |
| S13 | **Feste Ausschlussliste im Kern**, für alle Clients gleich: `.amicloudsync*`, `.DS_Store`, `._*`, `Thumbs.db`, `desktop.ini`, `~$*`, `*.part`, außerdem Namen mit `\` oder Steuerzeichen. Der Server würde `\` still zu `/` machen. | `sync.c` |

Speicher auf dem 68000: `SyncAction` belegt etwa 3 KB (3 × `AC_MAX_PATH`).
Für große Bäume sollen die Pfade dynamisch belegt werden. Das gehört in
diese Phase, damit S1–S13 den Speicherbedarf nicht weiter erhöhen.

## 4. Konflikte: neues Verhalten (alle Clients)

**Heute** überspringt der Amiga den Pfad, meldet ihn und lässt beide Seiten
unverändert. **Neu** werden wie bei ownCloud und Nextcloud beide Fassungen
behalten.

| Fall | Neues Verhalten |
|---|---|
| Beidseitig verschieden geändert, oder beidseitig neu mit verschiedenem Inhalt | Die lokale Fassung wird zuerst exklusiv zur **Konfliktkopie** umbenannt. Danach wird die Server-Fassung unter dem Originalnamen heruntergeladen und die Konfliktkopie als neue Datei hochgeladen (mit `create_only`). |
| Gleicher Inhalt auf beiden Seiten | kein Konflikt. **Ohne Server-MD5 (WebDAV)** erst in `.part` herunterladen und lokal vergleichen. Eine Kopie entsteht nur bei echtem Unterschied. Das verhindert Kopien-Lawinen bei AmiOwnCloud. |
| Entfernt gelöscht, lokal geändert | Die lokale Änderung gewinnt und wird **wieder hochgeladen**. Liegt sie in einem Ordner, wird dessen `SA_DEL_LOCAL` (`sync.c:597`) zu „Ordner auf dem Server anlegen“. |
| Lokal gelöscht, entfernt geändert | Die Server-Fassung wird **wieder heruntergeladen**. Der lokale Elternordner wird wieder angelegt. |
| Lokale Datei gegen Server-Verzeichnis | Die lokale Datei wird zur Konfliktkopie, das Verzeichnis wird normal heruntergeladen. |
| Lokales Verzeichnis gegen Server-Datei | Die Server-Datei wird lokal als Konfliktkopie heruntergeladen und **auf dem Server umbenannt** (neue Transport-Funktion `rename`, bei WebDAV `MOVE`). Danach wird das Verzeichnis hochgeladen. |

**Name der Konfliktkopie:** `<Stamm>_K<JJMMTT>-<HHMM>[.<Endung>]`, z. B.
`Brief_K261031-1402.txt` (entschieden: kurz, die Endung bleibt erhalten).
Der Zusatz enthält nur `_`, `-`, Ziffern und `K`, ist also auf dem Amiga,
unter Windows, macOS und Linux und auf dem Server erlaubt.

- Ortszeit über `cloudtime` (core).
- Zählt mit Kollisionszusatz `-2`, `-3` … und wird **exklusiv** angelegt.
  Neues `localfs_rename_noreplace()`: POSIX `rename` überschreibt ein Ziel,
  das gilt es zu vermeiden.
- **Namenslänge:**
  - Grenze `SYNC_NAME_MAX` in Zeichen, nicht in UTF-8-Bytes.
  - Amiga: 30 (FFS). OFS/FFS mit DOS\6/DOS\7 (ab OS 3.1.4/3.2), PFS3 und
    SFS erlauben mehr. Ob man das je Datenträger verlässlich abfragen kann,
    ist in Phase 0 zu klären; bis dahin gilt 30.
  - PC: 255.
- Zu lange Namen sind ohnehin ein allgemeines Problem. Vom Mac kommen
  lange Namen, die auf FFS nicht passen. Solche Dateien werden auf dem
  Amiga **übersprungen und gemeldet**, nie gekürzt, weil das Kürzen einen
  Duplikat-Upload erzeugen würde. In Phase 0 prüfen, ob FFS zu lange Namen
  still kürzt.

Umsetzung im Kern:

- `report_conflict()` wird zu `resolve_conflict()`, das Aktionen mit
  **Abhängigkeiten** einreiht (`depends_on`). Scheitert das Umbenennen,
  laufen Download und Upload nicht.
- Ein Abbruch nach dem Umbenennen ist sicher, `rollback_baseline_entry`
  sorgt für erneutes Herunterladen.
- `had_conflicts` bleibt nur für Konflikte, deren Auflösung scheitert.
  Dafür bleibt das bisherige Verhalten erhalten.
- Die Auswahl zwischen `KEEP_BOTH` (Standard) und `SKIP` (bisheriges
  Verhalten) bleibt als Option.
- `SyncSummary` bekommt die ersten N Konfliktpfade für die Anzeige.
- Neue Schnittstelle `cloudsync_run_ex(t, local, remote, const SyncOptions *,
  Progress *, SyncSummary *)`. Sie ersetzt `cloudsync_run` und
  `cloudsync_run_t`; die vier Aufrufer werden angepasst (keine Altlasten).

## 5. Schutz vor Massenlöschung (alle Clients)

S1 (Scan-Fehler sind fatal) und S3 (Identität) decken die häufigsten
Ursachen ab. Zusätzlich gilt:

- **Schwelle:** Ist die Zahl der Löschungen auf einer Seite größer als
  `max(SYNC_DEL_MIN, SYNC_DEL_RATIO × Einträge im Stand)`, fragt der Kern
  über `ui_confirm()`. Startwerte: 20 Dateien oder 50 %.
  - Beide Werte sind Felder in `SyncOptions`. Sie lassen sich später ohne
    Kernänderung anpassen (entschieden: vorläufig so, eventuell später
    ändern).
- **„Nein“ heißt eindeutig wiederherstellen:**
  - Die Einträge der betroffenen Pfade fliegen aus dem Stand.
  - Beim nächsten Lauf gelten die Dateien als „nur auf einer Seite“ und
    werden zurückkopiert (wie bei ownCloud).
  - Alle übrigen Aktionen dieses Laufs werden ausgeführt.
- **Oberflächen:**
  - Qt: ein Dialog mit Liste.
  - `amicloud-sync` und `amiowncloud-sync`: Abfrage auf der Konsole, für
    Skripte der Schalter `FORCE`.
  - `AmiOwnCloudSync` (Commodity): EasyRequest über einen ui-Hook.
- **Lokaler Papierkorb:**
  - Hook `localfs_set_trash_hook()` in **libcloudfs** (Kapselungsregel:
    Dateisystem → fs). Der Qt-Client setzt `QFile::moveToTrash`.
  - Ganze Ordner werden als Einheit verschoben.
  - Schlägt der Papierkorb fehl, wird **nicht** gelöscht, sondern gemeldet.
  - Auf dem Amiga wird weiterhin echt gelöscht.
- **Server-Papierkorb:** vorerst nicht (entschieden). Der lokale
  Papierkorb auf dem Desktop bleibt, er kostet nur einen Hook.

## 6. Desktop-Client

### 6.1 Name und Ort

- **„AmiCloud Desktop“**, die Datei heißt `amicloud-desktop`. So gibt es
  keine Verwechslung mit dem Kommandozeilenprogramm `amicloud-sync`.
- Verzeichnis `apps/amicloud-desktop-qt/` im Repo AmiCloud.
- Qt 6 Widgets, CMake, angelehnt an `amicloud-commander-qt`.

### 6.2 Aufbau

```
Tray (QSystemTrayIcon) ─ Status · Jetzt abgleichen · Pause · Ordner öffnen ·
   │                     Einstellungen · Aktivität · Beenden
   ├── SetupWizard   Server-URL → Anmeldung (App-Passwort) → lokaler Ordner → Auswahl
   ├── SettingsDialog, ActivityWindow (Aktionen, Konflikte, übersprungene Namen, Fehler)
   ├── SyncController (Zeitplan, Auslöser, genau EIN Netzzugriff zur Zeit)
   │      ├── RemoteWatcher: alle N s günstig fragen (status bzw. changes)
   │      ├── LocalWatcher: macOS FSEvents (direkt), sonst QFileSystemWatcher; entprellt
   │      └── SyncWorker (EIN QThread für ALLE Kern-Aufrufe: Sync, Ordnerbaum, Status)
   ├── Credentials: vorerst in der Konfiguration (Datei nur für den Benutzer lesbar)
   └── UiBridge: ui_hooks → Qt-Signale; ui_confirm blockierend im GUI-Thread
```

- **Ein einziger Worker-Thread für jeden Netzzugriff** über den Kern. Der
  Kern hat globale Zustände (`g_curl`, `g_last_status`, `g_hooks` …).
  Ordnerbaum, Status-Abfrage und Sync laufen dort nacheinander in einer
  Warteschlange.
- Eine Ordnerbaum-Abfrage während eines langen Laufs wartet. Besser:
  zwischen zwei Aktionen einschieben, oder die Baumdaten aus dem
  gespeicherten Stand zeigen.
- **Abbruch und Pause** über `ui_break_pending()` (S2) und Rückgabe 0 aus
  `ui_progress()`.
- **Bei 401 sofort aufhören** abzufragen und zur erneuten Anmeldung
  auffordern. Der Throttle (5 Fehlversuche) würde sonst alle Clients im
  Haushalt aussperren, auch den Amiga.
- **App-Passwort:** Heute lässt es sich nur in der Web-Oberfläche anlegen.
  Der Assistent öffnet dafür die Seite (Link) und nimmt das Passwort per
  Einfügen entgegen. Später ist eine Server-Erweiterung im Stil eines
  „Login-Flow“ denkbar.
- **Konfiguration** in `QSettings`. Format:
  - Server
  - Benutzer
  - lokaler Ordner
  - Startordner auf dem Server
  - Ausschlüsse
  - Intervall
  - Flag „schon abgeglichen“ (für S4)

- **Passwort:** Entschieden ist zunächst **kein Schlüsselbund und kein
  QtKeychain**.
  - Das App-Passwort steht in der Konfigurationsdatei. Die Datei bekommt
    die Rechte 0600, so wie `ENVARC:amicloud.cfg` auf dem Amiga.
  - Weil es ein App-Passwort ist, lässt es sich auf dem Server jederzeit
    widerrufen.
  - Der Zugriff läuft über eine kleine Klasse `CredentialStore`. Damit
    lässt sich später auf den Schlüsselbund umstellen, ohne den Rest zu
    ändern.

### 6.3 Ordnerauswahl

Modell wie bei ownCloud:

- **Ein** lokaler Ordner spiegelt den Server ab einem Startordner
  (Standard `/`).
- Die Auswahl wird als **Liste von Ausschlüssen** gespeichert, neue
  Server-Ordner sind also automatisch dabei.

Umsetzung:

- **Kern:** `SyncOptions.include` als Rückruf. Ausgeschlossene Pfade
  werden auf beiden Seiten übergangen.
- **Stand bei Ausschlüssen:**
  - Einträge ausgeschlossener Pfade werden aus dem Stand entfernt.
    Bleiben sie stehen und wird der Pfad später wieder aufgenommen, würde
    die fehlende lokale Kopie als Löschung gelten.
  - Wird die Auswahl geändert, ist der Schnellweg für **einen** Lauf
    abgeschaltet (`sync.c:461`). Sonst käme ein wieder aufgenommener
    Ordner erst nach der nächsten Server-Änderung.
- **Abgewählter Ordner, der lokal existiert:** Der Client fragt
  „In den Papierkorb“ oder „Behalten (dann nicht mehr abgeglichen)“. Der
  Kern löscht ihn nie selbst.
- **Server-Umbenennung eines ausgeschlossenen Ordners:** Der neue Name ist
  nicht ausgeschlossen und wird heruntergeladen. Das ist so gewollt, weil
  die Ausschlüsse an Pfaden hängen; es wird in der Doku erwähnt. Später
  ließen sich Ausschlüsse an die Server-ID binden.
- **Fallback:** nur ein Startordner ohne Ausschlüsse. Einstellungsformat
  und Oberfläche bleiben gleich.

### 6.4 Schrittweiser Abgleich (große Ordner)

**Server-Seite:** neue optionale Transport-Funktion `changes(since)`, nur
für AmiCloud (bei WebDAV `NULL`). Vorher sind am Server nötig:

- Paginierung (`limit`, `has_more`, Fortsetzung über `change_seq`) und
  ein Pfadpräfix für den Startordner.
- Umbenennungen erzeugen Grabsteine für den alten Pfad, oder der Kern
  ordnet über `id` und `server_id` im Stand zu. Empfehlung: Grabsteine,
  weil das einfacher und für alle Clients gleich ist.
- Eine einheitliche Zeitbasis: Den mtime-Versatz zwischen `list` und
  `changes` klären (die Verbindung auf UTC setzen oder die Epoch speichern).
  Ohne diese Klärung **nicht** auf `changes` umstellen.
- Grabsteine werden nach N Tagen aufgeräumt. Wer länger nicht abgeglichen
  hat, macht einen vollen Abgleich. Dafür meldet der Server
  `410 since too old`.

Verhalten:

- Gespeichert wird nur `global_revision`, sie ist derselbe Zähler wie
  `change_seq`.
- Nach jedem N-ten Lauf oder bei Unstimmigkeiten folgt ein voller
  Abgleich.

**Lokale Seite:** wie heute ein voller Scan, schnell, weil nur Größe und
Zeit geprüft werden. Der Dateiwächter löst nur den Lauf aus. Ein
Teil-Scan je Verzeichnis ist eine spätere Optimierung.

### 6.5 Dateinamen und Plattformgrenzen

| Thema | Umgang | Modul |
|---|---|---|
| Unicode-Normalform (macOS teils NFD) | Lokale Namen werden beim Einlesen auf NFC gebracht und beim Zugriff zurückgeführt. | libcloudfs (`localfs_pc`, macOS) |
| Groß-/Kleinschreibung | S6 | sync + fs |
| Windows-Verbote (`CON`, `NUL` …, Punkt oder Leerzeichen am Ende) | überspringen und melden | libcloudfs (Windows) + sync-Meldung |
| Pfadlänge (Windows 260) | `\\?\`-Präfix in `localfs_win` | libcloudfs |
| Geöffnete Dateien (Windows sperrt sie) | Ersetzen scheitert → im nächsten Lauf wiederholen, als Hinweis anzeigen | sync |
| Symlinks | nicht folgen: `lstat` statt `stat` (`localfs_pc.c:100`), überspringen und melden | libcloudfs |
| Große Dateien über `MAX_UPLOAD` | Der Server meldet die Grenze in `status` (Erweiterung). Der Kern überspringt solche Dateien vorab und meldet sie, statt bei jedem Lauf 413 zu bekommen. Chunked Upload ist eine spätere Erweiterung. | Server, sync |
| Amiga-Metadaten (Kommentar, Schutzbits) | Der Desktop-Upload lässt sie auf dem Server unangetastet. Auf dem Amiga gehen sie beim Download über `.part` heute verloren. **Kern:** Nach dem Ersetzen übernimmt der Amiga den Kommentar vom Server (`setcomment`/`list`), sofern vorhanden. | libcloudfs (Amiga), sync |

### 6.6 Plattformen

**macOS (zuerst):**
- Entschieden: **nur Apple Silicon (arm64)**, Zielsystem ist ein M4.
- **Mindestversion macOS 15 Sequoia**, kein Universal Binary.
- Statusmenü, `LSUIElement`.
- Autostart mit `SMAppService`.
- **libcurl aus dem SDK**, nicht aus Homebrew, weil sie die
  System-Zertifikate nutzt.
- Nach `macdeployqt` ad hoc signieren (`codesign -s -`), sonst startet das
  Programm unter arm64 nicht.
- Paket als `.dmg`.
- **Ohne Developer-ID** (entschieden: vorerst nur für den Eigenbedarf):
  Beim ersten Start über Systemeinstellungen → Datenschutz & Sicherheit
  → „Trotzdem öffnen“.
- Dateiwächter über FSEvents direkt, nicht über `QFileSystemWatcher`.
- Für ~/Documents und ~/Desktop verlangt TCC eine Zustimmung. Erst S1
  verhindert, dass eine fehlende Berechtigung als „alles gelöscht“
  gedeutet wird.

**Windows:**
- Kern-Port:
  - `localfs_win.c` (UTF-8 ↔ UTF-16, `_wfopen`, `MoveFileExW`,
    `\\?\`-Pfade).
  - Alle direkten `fopen`/`remove`/`rename` in `md5.c`, `syncstate.c`,
    `sync.c`, `cmd.c` und `http_pc.c` laufen über libcloudfs.
  - Dateigrößen als 64 Bit (`long` → `int64_t` bzw. `off_t`), das betrifft
    auch die Strukturen.
- Build: libcurl aus vcpkg, MSVC oder MinGW.
- Paket: `windeployqt` + Inno Setup.
- Autostart über den Registry-Schlüssel `Run`.
- Ohne Signatur warnt SmartScreen.

**Linux:**
- StatusNotifierItem; GNOME braucht die AppIndicator-Erweiterung.
- Secret Service.
- Autostart über `~/.config/autostart`.
- Paket als AppImage.
- Werden die inotify-Grenzen erreicht, nur noch zeitgesteuert abgleichen.

## 7. Amiga-Seite

- **Automatisch über den Kern** kommen § 3, § 4 und § 5 in:
  - AmiCloud: `amicloud-sync`
  - AmiOwnCloud: `amiowncloud-sync`, `AmiOwnCloudSync`,
    `amiowncloud-synctest`
- **Zeichensatz Latin-1 ↔ UTF-8 (eigenes Teilprojekt, betrifft mehr als
  den Sync):**
  - Die reinen Umwandlungsfunktionen kommen nach libcloudcore.
  - Umgewandelt wird an der Grenze zum Amiga-Dateisystem: in
    `localfs_amiga` (Sync und Kommandozeile) und im **Handler**
    (`AMICLOUD:`/`OWNCLOUD:`). Intern arbeiten Kern und Transporte immer
    mit UTF-8.
  - Namen ohne Latin-1-Entsprechung (€, Emoji, CJK) werden auf dem Amiga
    übersprungen und gemeldet; der Handler zeigt sie gar nicht an oder mit
    Ersatzzeichen (in Phase 0 entscheiden).
  - Kommentare (`setcomment`) werden ebenfalls umgewandelt.
- **Oberfläche:**
  - Massenlöschung: EasyRequest-Hook in der Commodity, Schalter `FORCE` für
    die Shell-Befehle.
  - Konflikte und übersprungene Namen im Fenster der Commodity bzw. auf der
    Konsole.
- **Doku:** Guides und Handbücher (Abgleich, Konflikte, Umlaute).
- **Release** laut `core/docs/RELEASE.md`. Alle Clients mit derselben
  Kernversion.

## 8. Tests

1. **Host-Unit-Tests für den Kern (Pflicht, vor jeder Verhaltensänderung):**
   - `core/libcloudsync/tests/` mit `FakeTransport` (Server im Speicher,
     umschaltbar zwischen AmiCloud-Art mit MD5 und WebDAV-Art ohne MD5,
     dazu Fehlerinjektion bei `list`, `put`, `del`).
   - Je ein Fall pro Zeile der Entscheidungsmatrix sowie S1–S13, alle
     Konfliktfälle, Ausschlüsse, Massenlöschung, Abbruch und
     Wiederaufnahme, Wechsel der Identität, Kollision der Schreibung.
   - Aufruf über `make test` im Core.
   - **Zuerst das heutige Verhalten festschreiben**, dann ändern.
2. **Gegen einen echten Server:** lokaler PHP-Server mit Testdatenbank
   (migrate_001..005 plus neue Migrationen) und zwei Client-Ordnern, die
   abwechselnd ändern.
3. **Amiga im Emulator:**
   - Umlaute (Sync, Kommandozeile, Handler)
   - Namen mit 30 und 31 Zeichen
   - Konfliktkopien
   - Massenlöschung
   - Ctrl-C mitten im Scan
4. **Desktop von Hand:**
   - Ersteinrichtung und Auswahl
   - Pause und Abbruch
   - Netz weg mitten im Scan
   - Konflikt mit dem Amiga als Gegenseite
   - TCC-Ordner
   - Autostart

## 9. Risiken

| Risiko | Gegenmaßnahme |
|---|---|
| Datenverlust durch Kernänderungen | Unit-Tests vorab; S1–S13 vor den Konfliktkopien; Papierkorb; `SKIP` als Notbremse |
| Bestehende Datenrisiken (§ 2.1) bis zum Release der Sicherheitsphase | Phase 1 zuerst und schnell ausliefern; bis dahin in der Doku vor WebDAV-Ordnerlöschungen warnen |
| Amiga- und PC-Kern laufen auseinander | ein Kern, gleiche Version in allen Releases |
| Zeichensatz-Umstellung verändert das Verhalten der Handler | eigenes Teilprojekt mit Emulatortests; Handler und Sync gemeinsam umstellen |
| Kern nicht threadsicher | ein Worker-Thread für alle Kern-Aufrufe |
| 68000-Speicher | `SyncAction`-Pfade dynamisch, Stand-Index sortiert |
| Unsignierte Programme | Doku; Signatur als spätere Entscheidung |

## 10. Phasen und Aufwand

Aufwand grob: S = bis 1 Tag, M = 2–4 Tage, L = 1–2 Wochen, jeweils
Arbeit an Code und Tests.

| # | Inhalt | Aufwand |
|---|---|---|
| 0 | Prüfen im Emulator: Umlaute (Kommandozeile, Sync, Handler), FFS bei zu langen Namen, Abfrage der Namensgrenze je Datenträger; Zeitbasis von `changes` gegenüber `list` am Server | S |
| 1 | Kern-Unit-Tests (`FakeTransport`), heutiges Verhalten festschreiben | M |
| 2 | **Datensicherheit S1–S13** und `cloudsync_run_ex`; Server: `if_revision`/`create_only`, `max_upload` in `status`; Amiga: `FORCE`/EasyRequest. **Release für alle Amiga-Programme.** | L |
| 3 | Konfliktkopien (§ 4) und Massenlöschungsschwelle (§ 5) im Kern, Transport `rename`; Amiga im Emulator testen; **Release** | M–L |
| 4 | Zeichensatz Latin-1 ↔ UTF-8 (Kern, `localfs_amiga`, beide Handler, Kommentare); **Release** | M–L |
| 5 | macOS-Client Grundstufe: Assistent, Keychain, Startordner mit Auswahlbaum, Abgleich auf Knopfdruck und nach Intervall, Tray, Aktivität, Pause | L |
| 6 | macOS: FSEvents, Papierkorb, Autostart, `.dmg`, SDK-curl, Signatur; Server: `changes` mit Paginierung, Grabsteine bei Umbenennungen, einheitliche Zeitbasis; `changes` im Kern. **Mac-Release** | L |
| 7 | Windows: Kern-Port (localfs_win, 64-Bit-Größen, alle direkten Dateizugriffe), Client-Anpassungen, Installer | L |
| 8 | Linux: AppImage, Secret Service, StatusNotifier | M |
| später | Server-Papierkorb, chunked Upload, Ausschlüsse an Server-IDs, Login-Flow für App-Passwörter, mehrere Konten | – |

Phase 4 kann auch vor Phase 3 kommen, wenn Umlaute auf dem Amiga
dringender sind. Für einen gemeinsamen Ordner Amiga ↔ Mac ist sie
spätestens vor dem Mac-Release nötig.

## 11. Entscheidungen (31.10.2026)

| # | Frage | Entscheidung |
|---|---|---|
| 1 | Name der Konfliktkopie | kurz, die Endung bleibt erhalten, auf allen Systemen erlaubt: `Brief_K261031-1402.txt` |
| 2 | Entfernt gelöscht, lokal geändert | wieder hochladen |
| 3 | Schwelle der Massenlöschung | 20 Dateien / 50 % als Startwerte, als Optionen einstellbar, wird eventuell später geändert |
| 4 | Signatur | keine, vorerst nur für den Eigenbedarf |
| 5 | macOS | nur Apple Silicon, ab macOS 15 Sequoia |
| 6 | Passwortspeicher | noch kein Schlüsselbund und kein QtKeychain: Konfigurationsdatei mit Rechten 0600, hinter `CredentialStore` |
| 7 | Programmname | „AmiCloud Desktop“ (`amicloud-desktop`) |
| 8 | Konfliktkopien gegen Umlaute | beides; Reihenfolge egal, also wie geplant (Phase 3, dann 4) |
| 9 | Server-Papierkorb | vorerst nicht; den lokalen Papierkorb auf dem Desktop gibt es trotzdem |
| 10 | Bestand / Migration | der Abgleich ist noch nirgends im Einsatz: keine Migration, keine Kompatibilitätsschichten (§ 1) |
