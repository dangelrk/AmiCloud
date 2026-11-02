# Plan: AmiCloud Desktop – Ordnerabgleich für macOS, Windows, Linux

Stand: 01.11.2026 · Status: Fassung 5; Phasen 0–4 umgesetzt (§ 13–15),
Phase 5 umgesetzt (§ 16), Phase 6 umgesetzt bis auf Deploy und Release (§ 17). Entscheidungen in § 11, Befunde aus Phase 0 und 1
in § 12 (S14–S16 neu). Der Entwurf wurde von einem Subagenten gegen den
Code geprüft und die Befunde eingearbeitet.

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
| S14 | **Kein Fortschreiben von `global_revision` nach Fehlern.** Scheitert im Lauf irgendeine Aktion, bleibt der alte Wert (bzw. 0) im Stand, damit der nächste Lauf vollständig scannt. Heute wird ein gescheiterter Download nie wiederholt, und eine spätere lokale Änderung überschreibt die neuere Server-Fassung (Test belegt, § 12). | `sync.c` |
| S15 | **Zu lange Namen erkennen statt kürzen lassen.** Klassisches FFS kürzt still auf 30 Zeichen, auch beim Öffnen, und überschreibt dabei gleich beginnende Namen (§ 12). Vor dem Lauf wird die Grenze des Datenträgers durch Anlegen einer Probedatei mit langem Namen ermittelt; längere Namen werden übersprungen und gemeldet. | `localfs_amiga`, `sync.c` |
| S16 | **Zeitzone in allen Sync-Programmen setzen.** `amicloud-sync`, `amiowncloud-sync` und `AmiOwnCloudSync` rufen `localfs_set_tz()` nicht auf und stempeln lokale Dateien in UTC; Shell und Handler rechnen mit `gmt_offset`/`dst`. | Apps beider Projekte |

Speicher auf dem 68000: `SyncAction` belegt etwa 3 KB (3 × `AC_MAX_PATH`).
Für große Bäume sollen die Pfade dynamisch belegt werden. Das gehört in
diese Phase, damit S1–S16 den Speicherbedarf nicht weiter erhöhen.

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
- **Zeichensatz ↔ UTF-8 (eigenes Teilprojekt, betrifft mehr als den
  Sync), über `codesets.library`** (entschieden 31.10.2026):
  - Die Amiga-Programme benutzen `codesets.library` (LGPL 2.1, Aminet
    `util/libs/codesets`, ab 6.22). `CodesetsFindA(NULL, NULL)` liefert
    den im System eingestellten Zeichensatz. Damit funktionieren auch
    Polnisch, Tschechisch und Kyrillisch (`ISO-8859-2`, `AmigaPL`,
    `Amiga-1251`), nicht nur Latin-1.
  - Die Library wird **vorausgesetzt, aber nicht mitgeliefert** (andere
    Programme wie YAM brauchen sie auch; dynamisch benutzt, daher keine
    Lizenzfragen). Der Installer prüft, ob sie in `LIBS:` liegt, und nennt
    sonst die Aminet-Adresse. Fehlt sie zur Laufzeit, melden die Programme
    das klar; die Handler öffnen sie erst beim ersten Zugriff, nicht beim
    Mounten.
  - In libcloudcore kommt eine dünne Schnittstelle (`cs_to_utf8()`,
    `cs_from_utf8()`), auf dem Amiga über `codesets.library`, auf dem PC
    ein Durchreichen. Umgewandelt wird an der Grenze zum Amiga-Dateisystem:
    in `localfs_amiga` (Sync und Kommandozeile) und in beiden **Handlern**
    (`AMICLOUD:`/`OWNCLOUD:`). Kern und Transporte arbeiten immer mit UTF-8.
  - **Zerlegte Zeichen (NFD)**, etwa vom Mac „a“ + Trema, setzt
    `codesets.library` nicht zusammen. Das übernimmt eine eigene kleine
    Tabelle vor der Umwandlung; der Desktop-Client lädt ohnehin nur NFC hoch.
  - **Nichts wird übersprungen** (entschieden 31.10.2026): Nicht
    darstellbare Zeichen werden umgesetzt (`CSA_MapForeignChars`, sonst
    Ersatzzeichen `_`). Kompromiss: Namen sehen auf dem Amiga anders aus,
    schlimmstenfalls gibt es Dateien doppelt oder als Konflikt.
  - **Pflicht dabei: verlustbehaftete Namen dürfen nie zu Löschungen
    führen.** Ohne Vorkehrung sähe der nächste Lauf `Preis_.txt` als neue
    lokale Datei (Upload) und `Preis€.txt` als lokal gelöscht (Löschen auf
    dem Server). Deshalb speichert der Stand je Eintrag den **lokalen
    Namen**, wenn er vom Server-Namen abweicht (Hin- und Rückweg ergeben
    nicht denselben UTF-8-Namen). Der lokale Scan ordnet solche Namen über
    den Stand wieder dem Server-Namen zu. Die Richtung Amiga → UTF-8 ist
    immer verlustfrei, die Zuordnung ist nur für heruntergeladene Namen
    nötig.
  - Ergeben zwei Server-Namen denselben lokalen Namen, wird der zweite wie
    bei S6 als Konfliktkopie angelegt (§ 4).
  - Der Handler zeigt umgesetzte Namen an und übersetzt sie beim Zugriff
    über die zuletzt gelesene Verzeichnisliste zurück.
  - Override `charset=` in `amicloud.cfg`, weil OS 3.x keinen
    Systemzeichensatz kennt und `codesets.library` ihn aus der
    Locale-Sprache errät.
  - Unit-Tests: Zuordnung, Kollision und Rückweg mit einer Fake-Umwandlung
    auf dem Host; die echte Library nur im Emulator.
  - Der verwendete Zeichensatz gehört zur **Identität des Stands** (S3).
    Wechselt er, gilt der nächste Lauf als Erstabgleich und löscht nichts.
  - Kommentare (`setcomment`) werden ebenfalls umgewandelt.
- **Mindest-CPU 68020** (entschieden 31.10.2026): AmiSSL 5 und
  `codesets.library` laufen ohnehin erst ab dem 68020, und ein 68000 ist
  für TLS zu langsam. Alle Amiga-Programme werden mit `-cpu=68020` gebaut
  (`CPU` in allen `Makefile.amiga-cross`, `Build-Amiga` neu erzeugen). Die
  Installer prüfen die CPU (`(database "cpu")`) und brechen auf einem 68000
  mit einer Meldung ab. `aminet.readme` und `INSTALL.md` aller drei
  Projekte nennen heute noch „68000 or better“ und werden korrigiert.
  Umsetzung mit dem Release von Phase 2.
- **Virenprüfung über `xvs.library`, optional** (Vorschlag 31.10.2026):
  - Dynamisch benutzt, nicht mitgeliefert (Freeware, nur nicht-kommerziell
    weitergeben). Fehlt sie, wird nicht geprüft. Läuft auch auf dem 68000.
  - Neue Funktion `localfs_virus_check()` in **libcloudfs** (lokale
    Dateien → fs); auf dem PC ein Nichtstun.
  - **Download:** die `.part`-Datei vor dem Ersetzen prüfen. Bei Fund nicht
    installieren, `.part` löschen, melden; die Server-Datei bleibt.
  - **Upload:** lokale Datei vor dem Hochladen prüfen, bei Fund nicht
    hochladen und melden. So verbreitet ein verseuchter Amiga nichts über
    die Cloud.
  - `xvsCheckFile()` braucht die ganze Datei im Speicher: nur bis zu einer
    einstellbaren Größe prüfen (Vorschlag 2 MB). Gepackte Programme
    erkennt sie nur entpackt (`xfdmaster.library`, später optional).
  - Zunächst Sync und Kommandozeile, nicht der Handler.
  - Umsetzung nach Phase 2 (eigener kleiner Schritt).
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
| 2 | **Datensicherheit S1–S16** und `cloudsync_run_ex`; Mindest-CPU 68020; Server: `if_revision`/`create_only`, `max_upload` in `status`; Amiga: `FORCE`/EasyRequest. **Release für alle Amiga-Programme.** | L |
| 3 | Konfliktkopien (§ 4) und Massenlöschungsschwelle (§ 5) im Kern, Transport `rename`; Amiga im Emulator testen; **Release** — *erledigt bis auf Release, § 15* | M–L |
| 4 | Zeichensatz ↔ UTF-8 über `codesets.library` (Kern, `localfs_amiga`, beide Handler, Kommentare, NFC), Installer-Prüfung, lokaler Name im Stand; parson schreibt ganze Zahlen als Ganzzahl (vbcc-`printf` gibt 986,0 als `985.999999999999` aus, § 13); optional Virenprüfung über `xvs.library`; **Release** | M–L |
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
| 5 | macOS | nur Apple Silicon, ab macOS 15 Sequoia (seit 02.11.2026: ab macOS 26, weil die Homebrew-Bibliotheken es verlangen) |
| 6 | Passwortspeicher | noch kein Schlüsselbund und kein QtKeychain: Konfigurationsdatei mit Rechten 0600, hinter `CredentialStore` |
| 7 | Programmname | „AmiCloud Desktop“ (`amicloud-desktop`) |
| 8 | Konfliktkopien gegen Umlaute | beides; Reihenfolge egal, also wie geplant (Phase 3, dann 4) |
| 9 | Server-Papierkorb | vorerst nicht; den lokalen Papierkorb auf dem Desktop gibt es trotzdem |
| 10 | Bestand / Migration | der Abgleich ist noch nirgends im Einsatz: keine Migration, keine Kompatibilitätsschichten (§ 1) |
| 11 | Zeichensatz auf dem Amiga | `codesets.library` voraussetzen, nicht mitliefern (Systemzeichensatz statt festem Latin-1) |
| 13 | Nicht darstellbare Zeichen | umsetzen statt verbieten oder überspringen; doppelte Dateien oder Konflikte sind hinnehmbar, Löschungen nicht (lokaler Name im Stand) |
| 12 | Mindest-CPU | 68020; OS 3.x mit Internet auf einem 68000 ist praktisch nicht nutzbar, AmiSSL 5 braucht ihn ohnehin |

## 12. Befunde aus Phase 0 und 1 (31.10.2026)

### Unit-Tests (Phase 1)

`core/libcloudsync/tests/` mit `FakeTransport` (AmiCloud-Art mit MD5 und
Revision, WebDAV-Art mit ETag und rekursivem DELETE, Fehlerinjektion und
Hooks für Wettläufe). Aufruf: `make test` im Core, gebaut mit
AddressSanitizer und UBSan, ohne libcurl.

- **40 Tests** halten das heutige Verhalten fest: Entscheidungsmatrix für
  beide Transporte, Ordner, Löschbäume, Zeitangleich, Probelauf,
  Ausschlüsse, Fehler mit Rücknahme im Stand, `global_revision`-Abkürzung,
  Stand speichern/laden, kaputter Stand.
- **11 RISK-Tests** prüfen das sichere Verhalten und scheitern heute
  erwartungsgemäß (XFAIL). Damit ist jedes Risiko belegt: S1 (dreimal),
  S2, S3, S5, S6, S7, S8, S9 und S14. Wird ein Risiko behoben, meldet der
  Test XPASS und zählt als Fehler, bis die RISK-Markierung entfernt ist.
- **Neu gefunden (S14):** Ein gescheiterter Download wird nie wiederholt,
  weil `global_revision` trotzdem fortgeschrieben wird. Ändert man die
  Datei danach lokal, überschreibt der Upload die neuere Server-Fassung,
  ohne Konflikt.
- Keine Speicherfehler unter ASan/UBSan.

### Lange Namen auf dem Amiga (Emulator, OS 3.2.3)

- Klassisches FFS (`DOS\1`, auf RAD: formatiert) **kürzt stillschweigend**
  auf 30 Zeichen, beim Anlegen und beim Öffnen. Zwei Namen mit gleichen
  ersten 30 Zeichen landen in derselben Datei; die zweite überschreibt
  die erste ohne Fehlermeldung.
- Folge für den heutigen Sync: Ein langer Server-Name wird lokal gekürzt
  angelegt, der nächste Lauf lädt die gekürzte Fassung als neue Datei hoch
  und löscht das Original auf dem Server („lokal gelöscht“). → S15.
- `DOS\7` (System:, Dev: im Emulator), `RAM:` und das Host-Verzeichnis
  `Setup:` nehmen mindestens 50 Zeichen.
- `id_DiskType` aus `Info()` taugt **nicht** zur Erkennung: `RAM:` meldet
  `DOS\0`, kann aber lange Namen; Handler melden ebenfalls `DOS\0`.
  Entscheidung für S15: **Probedatei** mit langem Namen im Sync-Ordner
  anlegen, Namen per `Examine()` zurücklesen, wieder löschen. Das
  funktioniert mit jedem Dateisystem (FFS, PFS3, SFS, Handler).

### Umlaute (Emulator)

| Weg | Ergebnis |
|---|---|
| Sync, Server-Name UTF-8 → Amiga | wird mit den UTF-8-Bytes angelegt: `GrÃ¼Ãe.txt`, `Preisâ¬.txt`. Hin und zurück stabil, aber unlesbar. |
| Sync, lokaler Latin-1-Name → Server | `Path is not valid UTF-8`, bei jedem Lauf ein Fehler, keine Datei |
| `AMICLOUD:` lesen | gleiche UTF-8-Bytes wie beim Sync |
| `AMICLOUD:` schreiben/umbenennen mit Umlaut | scheitert mit irreführender Meldung („Objekt nicht gefunden“ bzw. „Ungültiger Lock-Zugriff“) |
| `OWNCLOUD:` schreiben mit Umlaut | **gelingt**, der ownCloud-Server wandelt Latin-1 selbst in UTF-8 um. Die Datei ist danach unter dem geschriebenen Namen nicht mehr auffindbar (`Ãpfel` statt `Äpfel`). |

Für Phase 4 heißt das: Die Umwandlung muss in beide Richtungen und in
beiden Handlern kommen, über `codesets.library` (§ 7). Nicht darstellbare
Zeichen werden umgesetzt, nicht verboten (Entscheidung 13).

### Zeitbasis am Server

- `list`/`info` liefern `filemtime()`, also echte Epochensekunden.
- `changes` liefert `UNIX_TIMESTAMP(modified_at)`. `modified_at` ist ein
  `DATETIME` in Ortszeit: beim Upload `NOW()` (MySQL, Zeitzone `SYSTEM`),
  bei `setdate` die Ortszeit aus PHP (`AMICLOUD_TIMEZONE`).
- Produktion: MySQL `SYSTEM` = Host-Zeitzone MEZ/MESZ, PHP
  `Europe/Berlin`. Das passt zusammen, außer in der doppelten Stunde bei
  der Umstellung auf Winterzeit: `2026-10-25 02:30:00` gibt es zweimal,
  MySQL nimmt immer die spätere. Dateien aus der ersten dieser Stunden
  liegen in `changes` um 3600 s daneben. Läuft MySQL einmal in UTC, liegen
  alle per `setdate` gesetzten Zeiten um 1–2 h daneben.
- **Zusätzlich:** 123 von 167 Zeilen haben `change_seq = 0` (vor der
  Migration angelegt). Sie fehlen in `changes?since=0` vollständig.
- Für Phase 6: `changes` muss Epochensekunden ohne Ortszeit liefern
  (eigene Spalte mit Epoche oder `filemtime()`), und eine Migration muss
  allen Zeilen ein `change_seq` geben. Bis dahin bildet der Client seinen
  Erststand aus `list`, nie aus `changes`.
- `upload` liefert schon heute `md5`, `revision` und `mtime` zurück; für
  S10 ist auf dem AmiCloud-Server nichts zu tun, nur Transport und Kern.

### Zeitzone auf dem Amiga (Nebenbefund)

- Die Sync-Programme setzen keine Zeitzone (→ S16). Heruntergeladene
  Dateien tragen deshalb UTC-Zeit, im Emulator eine Stunde zu früh.
- `gmt_offset` und `dst` in `amicloud.cfg` werden von Hand gepflegt. Im
  Emulator steht noch `dst=1`, seit dem 25.10. gilt aber Winterzeit; der
  Handler zeigt deshalb Zeiten eine Stunde zu spät. Für später: Vorgabe
  aus `locale.library` (`loc_GMTOffset`) übernehmen.

## 13. Stand der Umsetzung Phase 2 (31.10.2026)

Alle Maßnahmen S1–S16 sind im Kern, alle elf RISK-Tests aus Phase 1 sind
grün, dazu Tests für jede Maßnahme (67 Tests, `make test` im Core).

| Commit (Core) | Inhalt |
|---|---|
| `0f14906` | S1 Scan-Fehler/Speichermangel → Abbruch, S2 Ctrl-C, S9, S14 |
| `d57d22b` | `cloudsync_run_ex` + `SyncOptions`, `amicloud_transport.c`, Stand mit Format und Identität (S3), S4 |
| `2257ce8` | S5 Ordner nur löschen, wenn alles darin gelöscht wird (sonst Gegenseite neu anlegen), S6, S7, S13 |
| `3c4b970` | S8 bedingter Upload (`SyncPutCond`, 412), S10 Server-Metadaten nach Upload |
| `3830b40` | S11 Zwischenstand alle 50 Aktionen/120 s, S12 sortierter Stand |
| `1c717d4` | S15 Namenslänge per Probedatei, Mindest-CPU 68020 |
| `ea567df` | WebDAV: `If-Match` braucht Anführungszeichen, `&quot;` im ETag |

- **S4 ohne Flag:** Mit vorhandenem Stand wird die Server-Wurzel nie neu
  angelegt; ein Flag `SYNC_EXPECT_EXISTING` ist nicht nötig.
- **S16:** `localfs_set_tz()` in `amicloud-sync`, `amiowncloud-sync`,
  `AmiOwnCloudSync` und `amiowncloud-synctest`.
- **Server** (deployt 31.10.2026): `upload` mit `if_revision` und
  `create_only` (412), `id` in der Antwort, `max_upload` in `status`.
- **68020:** alle Makefiles, `Build-Amiga`, Installer brechen auf
  68000/68010 ab, Doku korrigiert.
- **Emulator** (OS 3.2.3, klassisches FFS auf RAD:): Namensgrenze 30
  erkannt und übersprungen; Abbruch bei gelöschtem Server-Ordner;
  Ortszeit; fremder Stand (S3) beim Wechsel AmiCloud → ownCloud;
  bedingter Upload gegen ownCloud (Fehler gefunden und behoben); Ctrl-C
  mitten im Lauf (Stand und Server stimmen überein, der nächste Lauf
  macht den Rest).
- **Neuer Befund:** Ist die lokale Platte voll, lässt sich der Stand nicht
  schreiben (wird gemeldet). Bei WebDAV gelten die gerade übertragenen
  Dateien dann im nächsten Lauf als Konflikt, bis Phase 3 (`.part`
  vergleichen) kommt.
- Doku: Guides und Handbücher von AmiCloud und AmiOwnCloud
  („Schutz vor Datenverlust“).
- Offen für das Release 0.2: Installer im Emulator gegen die neue
  CPU-Prüfung testen, Release laut `core/docs/RELEASE.md` (Bauen und
  Taggen am selben Tag).

### Nachtrag Release 0.2 (31.10.2026)

- Unabhängige Prüfung durch einen Subagenten; behoben vor dem Release:
  Umbenennen am Server bei gleicher Revision (Datei-ID und MD5 werden
  jetzt verglichen), unbedingtes Löschen am Server (`delete` mit
  `if_revision`/`if_id`, WebDAV `If-Match`, Ordner werden vor dem Löschen
  neu gelesen), ausgeschlossene Dateien blockierten das Löschen ihres
  Ordners, Zwischenstand quadratisch, fehlendes lokales Laufwerk,
  unsicheres Ersetzen (`localfs_replace`).
- Im Emulator gefunden: Die C-Bibliothek von vbcc (`vc.lib`, `-lmieee`)
  gibt mit `printf("%1.17g")` manche ganzen Zahlen falsch aus (986,0 →
  `985.999999999999`); `strtod` liest korrekt. parson schrieb so Datei-IDs
  in den Stand, `(long)` schnitt auf 985 ab. Behoben durch `AC_DTOL()`
  (runden beim Einlesen); in Phase 4 schreibt parson ganze Zahlen
  zusätzlich per `%ld`. Fehlerbericht an vbcc sinnvoll.
- Release 0.2 am 31.10.2026 veröffentlicht (Tags `v0.2` in allen vier
  Repos, GitHub-Releases, AmiSuite).

## 14. Stand der Umsetzung Phase 4 (31.10.2026)

- **parson** schreibt ganze Zahlen per `%ld` (vbcc-`printf`-Fehler, § 13).
- **`libcloudcore/charset.c`:** `cs_open`, `cs_to_utf8`, `cs_from_utf8`,
  `cs_nfc`; auf dem Amiga über `codesets.library` (Sprungtabelle aus der
  `.fd`, keine fremden Header), ohne Bibliothek eingebautes ISO-8859-1.
  Nicht darstellbare Zeichen und das `?`, das die Bibliothek dafür setzt,
  werden zu `_`. NFC-Tabelle U+00C0–U+024F generiert
  (`tools/gen-nfc-table.py`). Einstellung `charset=`.
- **Sync-Kern:** Schlüssel immer UTF-8; lokale Pfade je Bestandteil
  umgewandelt. Die vorgesehene Speicherung des lokalen Namens im Stand
  war nicht nötig: Die Umwandlung ist deterministisch, eine Zuordnung aus
  Server-Liste und Stand (`NameMap`) ordnet verlustbehaftete Namen zurück.
  Gleiche lokale Form für verschiedene Server-Namen wird übersprungen. Der
  Zeichensatz gehört zur Identität. 87 Tests (künstlicher ASCII-Zeichensatz
  auf dem Host).
- **Nebenbei gefunden:** `cmd.c` hat Pfade nie prozentkodiert – Namen mit
  Leerzeichen, `&`, `#`, `+` scheiterten bei allen AmiCloud-Programmen.
  `remote_resolve()`: `/x` an der Wurzel ergab auf dem Amiga einen
  relativen Pfad; `:dir` wird jetzt erkannt.
- **Programme:** `amicloud`, `amicloud-shell`, `amicloud-sync`,
  MUI-Commander (`PanelEntry.uname`), AmiOwnCloud-Sync-Programme, beide
  Handler-Worker (`cs_list_name`/`cs_path_to_utf8` merken sich
  verlustbehaftete Namen der letzten Listen), Kommentare.
- **Installer** nennen eine fehlende `codesets.library`; Doku ergänzt.
- **Emulator:** Sync, Kommandozeile, Shell, Commander und beide Handler mit
  Umlauten, `€`, Leerzeichen und `&#+` getestet.
- **Virenprüfung** (Core 2568227): `localfs_virus_check` über
  `xvs.library` 33+ (bis 2 MB), im Sync, im Commander und in
  `amicloud get/put`; ohne Bibliothek keine Prüfung. Im Emulator getestet.

## 15. Stand der Umsetzung Phase 3 (31.10.2026)

Core 4d59922 und 8df1871, AmiCloud 43bc21f, AmiOwnCloud 5404200,
AmiVCS 4ebf124. Version 0.3 eingetragen, **noch kein Release**
(Entscheidung des Nutzers).

- **Konflikte** (§ 4): `SyncOptions.conflict_skip` (0 = beide behalten,
  Vorgabe; 1 = bisheriges Überspringen). Aktionen `SA_KEEP_BOTH` (Server-
  Fassung in `.part`, Inhalt vergleichen, gleich → nichts; sonst lokale
  Datei → Konfliktkopie, Server-Fassung an den Originalnamen, Kopie mit
  `create_only` hoch), `SA_FILE_AWAY`, `SA_RFILE_AWAY`. Gelöscht gegen
  geändert: die geänderte Fassung wird wieder übertragen.
  `SyncSummary.copies`.
- **Abweichungen vom Plan:**
  - Keine Abhängigkeiten zwischen Aktionen (`depends_on`); jede
    Konfliktaktion erledigt Umbenennen, Download und Upload selbst und
    stellt bei einem Fehler den alten Zustand her.
  - Kein Transport-`rename`: „lokaler Ordner gegen Server-Datei“ lädt die
    Server-Datei als Kopie, lädt sie unter dem neuen Namen hoch und löscht
    das Original bedingt (`if_revision`/`If-Match`).
  - Statt `localfs_rename_noreplace()` prüft `find_conflict_copy()` vorher
    auf ein freies Ziel (bis `-9`); das Restrisiko eines gleichzeitigen
    Anlegens im selben Augenblick ist hingenommen.
  - Der lokale Papierkorb (`localfs_set_trash_hook`) und der Qt-Dialog
    folgen mit dem Desktop-Client (Phase 5/6).
- **Massenlöschung** (§ 5): `guard_mass_deletion`, `del_min`/`del_pct`
  (20/50 %), `force_deletes`. „Nein“ nimmt nichts zurück, der nächste
  Lauf kopiert zurück; im Probelauf nur eine Warnung.
  `SyncSummary.kept`.
- **Programme:** `amicloud-sync`/`amiowncloud-sync` mit `-f`/`FORCE`
  (auch hinter den Ordnern), Konsolen-Rückfrage `[j/N]`;
  AmiOwnCloudSync fragt per EasyRequest („Löschen“/„Nicht löschen“).
- **Tests:** 97 Unit-Tests. `amiowncloud-synctest` T6 prüft die
  Konfliktkopie gegen einen echten WebDAV-Server (15/15 im Emulator).
  Im Emulator: Konfliktkopie (`brief_K261031-2237.txt`), Rückfrage bei 100
  Löschungen, Zurückkopieren nach „Nein“, `FORCE`.
- **Doku:** Handbücher und Guides beider Projekte.

## 16. Stand der Umsetzung Phase 5 (01.11.2026)

macOS-Client Grundstufe, `apps/amicloud-desktop-qt/` (Qt 6 Widgets,
CMake, `make qt`). Version 0.3, **noch kein Release**.

- **Kern:**
  - `SyncOptions.excludes`/`n_excludes`: Pfadpräfixe relativ zum
    Startordner. Sie werden auf beiden Seiten übergangen, ihre
    Stand-Einträge entfallen. Ein Stempel der Liste (MD5) steht im Stand
    (`selection`); eine geänderte Liste schaltet den Schnellweg für einen
    Lauf ab. `sync_path_excluded()` für die Clients.
  - 9 neue Tests, jetzt 106.
- **Befund, behoben:** parson las und schrieb Zahlen nach dem Locale des
  Prozesses. Qt setzt `setlocale(LC_ALL, "")`; unter `de_DE` war damit
  jede Antwort mit einer Zahl vor einem weiteren Feld ungültig. Auch der
  eigene Stand ließ sich nicht lesen, jeder Lauf wäre ein Erstabgleich
  gewesen, ohne Löschungen. Betroffen war auch der Qt-Commander, wenn er
  mit `LANG=de_DE` startet (aus dem Terminal; aus dem Finder meist im
  C-Locale). Jetzt gilt im Kern unabhängig vom Locale immer `.`.
- **Befund, behoben (S2):** Bricht Pause oder Ctrl-C die laufende
  Übertragung ab, zählt das nicht mehr als Fehler. Der Lauf gilt als
  abgebrochen, auch wenn es die letzte Aktion war.
- **Client:**
  - `CoreWorker` ist der einzige Thread für Kernaufrufe: Kontoprüfung,
    Ordnerliste und Lauf.
  - `CoreBridge` leitet `ui_hooks` als Qt-Signale weiter. `ui_confirm`
    blockiert den Worker, bis die Frage im GUI-Thread beantwortet ist.
  - `SyncController` übernimmt Zeitplan, Zustände, Pause und das Anwenden
    neuer Einstellungen erst nach dem laufenden Lauf. Papierkorb erst
    danach, mit der neuen Auswahl schon gespeichert.
  - `CredentialStore` speichert das App-Passwort in einer eigenen Datei,
    die von Anfang an 0600 hat (`QSaveFile`).
  - Assistent, Auswahlbaum, Einstellungen, Aktivität, Menüleiste mit
    gezeichnetem Vorlagen-Symbol.
- **401:** Vor jedem Lauf wird `status` abgefragt; bei 401/403 kommt
  kein Lauf und kein Zeitplan mehr, bis neue Zugangsdaten eingetragen
  sind. Ohne Netz wird mit Rückzug neu versucht (1, 2, 4, … Minuten bis
  zum Intervall).
- **Abweichungen:**
  - Kein `RemoteWatcher` mit eigenem Takt: Der Zeitplan startet den Lauf,
    der Schnellweg (`global_revision`) hält ihn billig.
  - Pause setzt den Break-Merker des Kerns (`ui_break_set`).
  - Der Papierkorb gilt für abgewählte Ordner (`QFile::moveToTrash`);
    `localfs_set_trash_hook` für Löschungen im Lauf folgt in Phase 6.
- **Tests:** `amicloud-desktop-selftest` läuft ohne Oberfläche gegen einen
  echten Server: lokaler `php -S` mit MariaDB im Scratchpad. Alle 47
  Prüfungen grün, unter `de_DE`. Geprüft:
  - Konto richtig und falsch (401)
  - zwei Clients mit und ohne Ordner B, Wiederaufnahme
  - Abwählen mit Papierkorb
  - Massenlöschung „Nein“ und Zurückkopieren
  - Pause während 40 MB und Fortsetzen
  - widerrufenes Passwort und Rückkehr
  - Logik des Auswahlbaums (Ab- und Anwählen unter einem
    ausgeschlossenen Ordner)

  **Oberfläche (01.11.2026, per AppleScript/Bedienungshilfen und echten
  Mausklicks gegen den Testserver):** Assistent komplett (falsches und
  richtiges Passwort, lokaler Ordner, Auswahlbaum mit Ab-/Teilauswahl,
  Server-Wurzel `/`), erster Abgleich, Statusmenü, Pause/Fortsetzen
  (Symbol, Menütexte, gespeicherter Zustand), Aktivität, Einstellungen
  (Anmeldung prüfen mit Belegung, gespeicherte Auswahl, Abwählen →
  Rückfrage → Papierkorb, Server unverändert), Beenden über das Menü.
  Dabei gefunden und behoben: Vorschlagspfad im Assistenten nicht
  übernehmbar (Pflichtfeld zählte erst nach Änderung), Zusatzschaltflächen
  waren Standardschaltfläche (Enter öffnete den Browser), Aktivität zeigte
  volle lokale Pfade. Noch offen: eigenes App-Symbol (`.icns`, Phase 6);
  die Rückfrage bei Massenlöschung nur im Selbsttest, nicht als Fenster.
- **Nächstes (Phase 6):** FSEvents, Papierkorb-Hook für Löschungen,
  Autostart (`SMAppService`), `.dmg` mit `macdeployqt` und Ad-hoc-
  Signatur; Server: `changes` mit Paginierung, Grabsteine, einheitliche
  Zeitbasis.

## 17. Stand der Umsetzung Phase 6 (01.11.2026)

Core db0fdaa und 82f1dda, AmiCloud f1d90ab, a5da0eb und 79e65cd.
**Noch nicht deployt, kein Release.**

- **Papierkorb** (§ 5): `localfs_set_trash_hook()`/`localfs_trash()` in
  libcloudfs. Ein auf dem Server gelöschter Ordner wandert als **Einheit**
  in den Papierkorb, wenn der Lauf alles darin löscht, jede Datei noch so
  ist wie beim Scan (S7) und auf der Platte genau so viele Einträge liegen
  wie Löschaktionen. Sonst geht es Datei für Datei. Scheitert der
  Papierkorb, wird nichts gelöscht und ein Fehler gemeldet. Amiga und
  Kommandozeile löschen wie bisher. Der Desktop setzt
  `QFile::moveToTrash`. 6 Tests.
- **FSEvents** (`LocalWatcher`): Ereignisse auf Dateiebene, Latenz 1 s.
  Namen, die nie abgeglichen werden (`.amicloudsync*`, `.DS_Store` …),
  werden ignoriert. Ein Lauf startet 3 s nach der letzten Änderung.
  Änderungen während eines Laufs, auch eigene Schreibzugriffe, führen zu
  genau einem Folgelauf, der dann nichts mehr zu tun hat. Im Selbsttest
  gemessen: 1 Lauf, danach Ruhe.
- **Autostart:** `SMAppService.mainApp`, Häkchen unter Einstellungen →
  Abgleich. Der Zustand liegt beim System. **Nicht getestet:** Das
  Eintragen eines Anmeldeobjekts hat die Sicherheitsprüfung der
  Sitzung abgelehnt (dauerhafte Systemänderung). Von Hand prüfen.
- **App-Symbol** aus `server/logo.svg` (`scripts/make_desktop_icon.sh`).
- **`.dmg`** (`scripts/make_desktop_dmg.sh`):
  - Release-Build und `macdeployqt`, die SVG/PDF-Plugins entfernt.
  - Das Skript bricht ab, wenn im Bundle noch etwas auf Homebrew zeigt.
  - Ad-hoc-Signatur, Image mit Verweis auf „Programme“, etwa 28 MB.
  - Geprüft: Das Programm aus dem Image lädt keine Bibliothek aus
    Homebrew (`DYLD_PRINT_LIBRARIES`).
- **Server** (`migrate_006.sql`):
  - `changes` mit `path`-Präfix, Seiten (`limit`, `has_more`, `next`),
    die nie eine `change_seq` teilen.
  - `mtime` aus `filemtime()` wie bei `list`, damit ist die Zeitbasis
    geklärt (§ 12). MD5 wie bei `list`.
  - Grabsteine werden nach 30 Tagen entfernt; ein älteres `since` ergibt
    `410`. Das Gegenstück ist `users.tombstone_seq`.
  - Umbenennen legt Grabsteine für die alten Pfade an. Am Ziel werden
    nur noch die Grabsteine gelöscht, deren Pfad neu belegt wird; vorher
    gingen dort alle verloren.
  - Zeilen mit `change_seq = 0` bekommen 1.
  - `scripts/test_changes.py` mit 25 Prüfungen, 410 von Hand geprüft.
- **Kern – Abgleich über `changes`:**
  - Hat sich `global_revision` bewegt, wird der letzte Stand mit dem
    Delta fortgeschrieben (`transport.changes`, nur AmiCloud).
  - Voll gelistet wird bei jedem 20. Lauf (`delta_runs` im Stand), bei
    410, bei Fehlern, nach mehr als 50 Seiten, bei offenen Konflikten,
    nach geänderter Auswahl und im Probelauf.
  - **Gefunden beim Bau:** Ein Grabstein für den Server-Ordner selbst
    (umbenannt oder gelöscht) hätte alles lokal löschen lassen. Jetzt
    führt er zur vollen Liste, und die bricht ab (S4).
  - FakeTransport mit Änderungsprotokoll: Alle AmiCloud-Tests laufen
    jetzt über das Delta. Zwillingstests vergleichen Delta und volle
    Liste über gemischte Folgen (Umbenennen, Ordner wird Datei und
    umgekehrt, Löschen und Neuanlegen, Konflikte); lokal und auf dem
    Server ist das Ergebnis gleich.
  - 119 Tests. Selbsttest gegen echten Server 56/56 mit 20 Delta-Abrufen
    ohne Rückfall. `amicloud-sync` nach Umbenennung am Server: eine
    `changes`-Anfrage, kein `list`.
  - `cmd_changes` ist neu: `AcChange.path` liegt im selben Block statt in
    einem eigenen 1-KB-Puffer. Auf dem Amiga sind Seiten 200 Zeilen lang.
    Alle Amiga-Programme bauen.
- **Abweichungen:**
  - `changes` mit Präfix statt einer Zuordnung über `server_id`.
  - Kein Purge-Job: Aufgeräumt wird beim Abruf von `changes`.
  - Ein Teil-Scan je Verzeichnis lokal bleibt eine spätere Optimierung.
- **Unabhängige Prüfung vor 0.3 (01.11.2026), behoben:**
  - Datenverlust: Ein Startordner ohne eigene Server-Zeile (nur durch
    Uploads darunter entstanden) bekam beim Löschen oder Umbenennen keinen
    Grabstein. Das Delta hätte alle lokalen Dateien gelöscht, S4 griff nicht.
    Jetzt prüft der Kern bei Löschungen in der Änderungsliste mit einem
    `list`, ob der Startordner noch existiert. Der Server schreibt zusätzlich
    immer einen Grabstein für den Pfad selbst, und Upload, `mkdir` und
    `rename` legen Elternordner-Zeilen an bzw. beleben sie wieder.
  - Papierkorb-Ordner als Einheit nur, wenn genau die gescannten Einträge
    darin liegen (nicht nur gleich viele).
  - Der Zwischenstand speichert die alte Auswahl. Ein abgebrochener Lauf,
    der einen Ordner wieder aufnimmt, listet danach erneut.
  - Relative Server-Wurzel (`amicloud-sync Work:x texte`): Der Transport
    gibt Pfade in der Form des Aufrufers zurück. Vorher ignorierte das
    Delta alle Änderungen.
  - Server: Zähler und Zeilen bei `delete` und `mkdir` in einer
    Transaktion. `rename` verschiebt keine alten Grabsteine mehr, das sparte
    unnötige 410-Antworten.
  - App: Beenden während eines nicht abbrechbaren Aufrufs stürzt nicht mehr
    ab. Der Text der Papierkorb-Frage nennt, dass lokal neue Dateien dann
    nur noch im Papierkorb liegen.
  - 122 Kerntests, `test_changes.py` 34/34.
- **Offen:**
  - Autostart von Hand testen.
