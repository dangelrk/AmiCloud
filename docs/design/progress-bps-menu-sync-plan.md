# AmiCloud Commander: byte-genauer Fortschritt, bps-Anzeige, Amiga-Menü/About, Panel-Sync, Prüfsummen-Anzeige

## Context

Die Amiga-MUI-Version des Commanders läuft grundsätzlich, hat aber noch Lücken gegenüber der Qt-Version bzw. gegenüber dem, was der Nutzer sich als nächstes wünscht:

1. Der Fortschrittsbalken bei Up-/Downloads springt in Datei-Schritten statt sich innerhalb einer Datei flüssig zu bewegen — bei Einzeldateien sogar sofort auf 100%, bevor überhaupt ein Byte übertragen wurde. Die echten Byte-Zahlen kommen bereits aus der HTTP-Schicht an, werden im GUI-Gauge aber verworfen.
2. Es gibt aktuell **keinerlei** Fortschrittsanzeige in den Konsolen-Tools (CLI/Shell) — der Konsolen-Hook ist ein reiner No-op. Der Nutzer möchte dort wie in den GUIs eine bps-Anzeige (Bytes/Sekunde).
3. Der Amiga-Client hat kein Menü und keinen About-Requester.
4. Beide GUI-Varianten (Qt und Amiga MUI) sollen einen Menüpunkt "Sync" bekommen, der den bereits in der Shell existierenden, aber noch ungetesteten Zwei-Wege-Sync (`SYNC`-Befehl, MD5+mtime-Abgleich) ausführt — bislang ist diese Logik komplett privat in `shell.c` und für die GUIs nicht erreichbar.
5. Beide GUI-Varianten sollen einen Menüpunkt "Prüfsumme anzeigen" für die aktuell selektierte(n) Datei(en) im aktiven Panel bekommen.
6. Zusätzlich soll die Amiga-Download-Prozentanzeige repariert werden: aktuell wird die Content-Length nicht geparst, bevor der komplette Response-Body gelesen ist, wodurch `total` beim Download immer `-1` ist (Nutzer hat sich explizit für diesen zusätzlichen, riskanteren Fix entschieden statt ihn auszulassen).
7. Zwei weitere, beim Durchlesen entdeckte Amiga-GUI-Bugs: die als "F2"/"F3"/… beschrifteten Buttons reagieren nicht auf die echten Funktionstasten (Ursache: kollidierender `MUIA_ControlChar`), und welches Panel gerade aktiv ist, ist nur an einer leicht zu übersehenden Statuszeile erkennbar, nicht am Panel selbst.

Alles baut auf bereits vorhandener Infrastruktur auf (`ui_hooks.h`/`ui_progress()`-Seam, `panel.h`'s bereits etablierten Präzedenzfall "Logik aus shell.c nach panel.c auslagern, damit GUIs sie mitbenutzen können", die vorhandenen `MUI_Request()`-Aufrufe als About-Vorlage). Keine der Änderungen erfordert neue Build-Dateien — alle betroffenen `.c`-Dateien sind bereits in jedem Zielsystem (`Makefile.pc`/`.amiga`/`.win`, `gui_qt/CMakeLists.txt`) eingebunden.

## 1. Byte-genauer Fortschritt (beide GUIs)

**`client/panel.h`** — `Progress`-Struct um ein Feld erweitern:
```c
typedef struct {
    int  done, total;
    int  ok, err;
    long bytes_done, bytes_total;
    long cur_file_size;   /* Größe der gerade laufenden Datei */
} Progress;
```

**`client/panel.c`** — `progress_prefix()` setzt zusätzlich `p->cur_file_size = file_size;` direkt nach dem bestehenden `p->done++`/`p->bytes_done += file_size`.

Damit ergibt sich für jeden `ui_progress(done, total, name)`-Callback während einer laufenden Multi-Datei-Operation die exakte Gesamt-Byte-Zahl:
```c
long overall = p->bytes_done - p->cur_file_size + done;
int pct = p->bytes_total > 0 ? (int)((overall * 100) / p->bytes_total) : 0;
```
Das ist keine Heuristik, sondern exakt — `bytes_done` wird in `progress_prefix()` bereits *vor* Transferbeginn um die volle Dateigröße erhöht (das ist auch die Ursache des "springt sofort auf 100%"-Bugs); durch Abziehen von `cur_file_size` und Addieren des echten `done` bekommt man "abgeschlossene Bytes aller vorherigen Dateien plus tatsächlicher Fortschritt der laufenden Datei".

- **`client/gui_amiga/gui.c`**: `mui_progress()` (aktuell ~Zeile 125–156) — die Gauge-Formel, die bislang nur `g_op->pr.done/total` (Datei-Zähler) nutzt, durch obige Byte-Formel ersetzen. Die `x/y`-Anzeige bleibt zusätzlich als Text erhalten.
- **`client/gui_qt/transferjob.h`/`.cpp`**: Signal `overallProgressChanged` von reinem Datei-Zähler auf `(bytesDone, bytesTotal, fileDone, fileTotal, name, bps)` umstellen; `hookProgress()` rechnet mit derselben Formel.
- **`client/gui_qt/mainwindow.cpp`**: `QProgressDialog` aus `bytesDone*100/bytesTotal` statt Datei-Zähler befüllen.

Der `do_rename_move()`-Pfad (direkter `cmd_get`/`cmd_put` ohne `CopyCtx`) bleibt unverändert (zeigt weiterhin nur Fortschritt der einen laufenden Datei — korrekt, da es sich immer nur um eine Datei handelt).

## 2. bps-Anzeige, überall (GUI + Konsole)

Zentral in **`client/ui_hooks.h`/`ui_hooks.c`** (einzige Datei, die in jedem Zielsystem bereits mitgebaut wird):
```c
typedef struct { long start_time, last_time, last_bytes, bps; } UiRateMeter;
void ui_rate_reset(UiRateMeter *m);
int  ui_rate_sample(UiRateMeter *m, long done);  /* liefert 1 höchstens 1x/Sekunde */
```
Basiert auf `time(NULL)` (1-Sekunden-Auflösung, plain C89, portabel auf vbcc/AmigaOS, MinGW, gcc/clang — kein plattformspezifischer Timer nötig). Throttling auf ca. 1 Update/Sekunde verhindert, dass bei 4–16 KB-Chunks ständig neu gezeichnet wird.

**Konsole** (`console_progress()` in `ui_hooks.c`, aktuell reiner No-op): wird zu einer echten `\r`-basierten Statuszeile:
- bekannte Gesamtgröße: `\r%-24.24s %6ld%% %6ld KB/s   `
- unbekannte Gesamtgröße (`total<=0`): `\r%-24.24s %6ldK gelesen %6ld KB/s   `
- Neuer Transfer/neue Datei wird über `name`-Wechsel oder `done < last_bytes` erkannt → `UiRateMeter` zurückgesetzt.
- `console_notify()`/`console_confirm()` schreiben vorher ein `\n`, falls gerade eine Fortschrittszeile offen ist, damit nachfolgende Meldungen nicht mit der Statuszeile kollidieren.
- Bekannter, akzeptierter kosmetischer Nebeneffekt: der einmalige `"(x/y p%) "`-Präfix aus `progress_prefix()` (`panel.c`) wird von der ersten `\r`-Zeile überschrieben — nur für einen Sekundenbruchteil sichtbar, unkritisch.
- Kommentar in `ui_hooks.h` ("CLI/shell bleibt byte-identisch") aktualisieren, da das jetzt bewusst nicht mehr gilt.

**Amiga-GUI** (`gui.c`): `UiRateMeter` an die Gauge-Textanzeige koppeln, bps zusätzlich zu Name+Prozent im `MUIA_Text_Contents`.

**Qt** (`transferjob.cpp`/`mainwindow.cpp`): `UiRateMeter`-Member in `TransferJob`, bps in den erweiterten Signalen mitschicken, im `QProgressDialog`-Label anhängen (`"… – 512 KB/s"`).

## 3. Amiga-Download-Prozentanzeige reparieren (Content-Length vorziehen)

**`client/http_amiga.c`**: `ssl_read_all()` (aktuell Zeile ~349–404) liest den kompletten Response roh ein und meldet `ui_progress(size, -1, name)` — `total` ist nie bekannt, weil `find_body()`/`extract_header()` (Content-Length wird heute überhaupt nicht geparst) erst *nach* vollständigem Lesen in `do_request()` aufgerufen werden.

Änderung: `ssl_read_all()` bekommt lokalen Zustand `size_t header_len = 0; long total_body = -1;`. Nach jedem angehängten Chunk, solange `header_len == 0`: `find_body(buf, size)` versuchen; sobald gefunden, `header_len` setzen und per `extract_header(buf, header_len, "Content-Length:", ...)` `total_body` bestimmen (leer/fehlend → bleibt `-1`, alter Fallback). Danach `ui_progress(size - header_len, total_body, name)` statt `ui_progress(size, -1, name)`. Vor Header-Ende bleibt es beim bisherigen `ui_progress(0, -1, name)`.

Da `find_body()`/`extract_header()` aktuell *unterhalb* von `ssl_read_all()` in derselben Datei definiert sind, werden Vorwärtsdeklarationen (oder Umsortieren) nötig. Uploads sind nicht betroffen (kennen `upload_fsize` bereits vorher). Dieser Teil ist der einzige noch nicht auf echter/emulierter Hardware verifizierte Codepfad des gesamten Plans — sorgfältig gegen die bestehenden `HLOG`-Traces prüfen.

## 4. Amiga MUI: Menü + About-Requester

**`client/gui_amiga/gui.c`**:
- Neues Include `<libraries/gadtools.h>` (dort leben `NM_TITLE`/`NM_ITEM`/`NM_END`/`NM_BARLABEL`/`struct NewMenu`, nicht in `<libraries/mui.h>`).
- Neue IDs im bestehenden `#define`-Block (Zeile ~37–60): `ID_ABOUT 8`, neuer Block `ID_SYNC 40`, `ID_CHECKSUM 41` (nur Slot 8/9 ist vor dem `ID_LEFT_*`-Block frei, reicht für 3 neue IDs nicht — daher neuer Block, passend zur bestehenden "gruppiert mit Lücken"-Konvention). "Beenden" nutzt die bestehende `ID_QUIT`.
- Neues `static struct NewMenu MenuData[]` (Projekt-Menü: Über…, Trenner, Sync…, Prüfsumme anzeigen, Trenner, Beenden), analog zum Referenzmuster in `mui38dev/MUI/Developer/C/Examples/Menus.c`.
- In `gui_run()`: `MUIA_Window_Menustrip, MUI_MakeObject(MUIO_MenustripNM, MenuData, 0),` auf das **Hauptfenster** (nicht auf `Application`) setzen — die beiden Dialog-Subfenster (`g_input_window`, `g_progress_window`) haben bereits `MUIV_Window_Menu_NoMenu` und erben nichts.
- Menü-IDs laufen durch denselben bestehenden `switch(id)` in der `MUIM_Application_Input`-Loop wie Buttons — keine neue Dispatch-Mechanik nötig.
- Neues `do_about()`, nach dem Vorbild von `do_info()`/den bestehenden `MUI_Request(g_app, NULL, 0, "Info", "_OK", msg)`-Aufrufen, Text spiegelt die vorhandenen `MUIA_Application_*`-Strings.

## 5. Panel-Sync über Menü (Qt + Amiga), Sync-Engine nach `panel.c` ausgelagert

Der bestehende `SYNC`-Befehl der Shell (`do_sync()` in `client/shell.c`, Zeilen ~277–678: rekursive Indizierung beider Seiten, Abgleich per Größe/MD5/mtime, `SA_UPLOAD/SA_DOWNLOAD/SA_MKDIR_*/SA_ALIGN_*`-Aktionsliste) ist heute komplett `static`/privat und an `ShellSession` gekoppelt. Er wird — analog zum bereits in `panel.h` dokumentierten Präzedenzfall ("Logik ohne REPL-Abhängigkeit aus shell.c nach panel.c verschoben, damit GUIs sie mitbenutzen können") — extrahiert:

**`client/panel.h`**: neuer Typ `SyncSummary` und
```c
void panel_sync(const AcConfig *cfg, const char *local_root, const char *remote_root,
                int dry_run, int verbose, Progress *pr, SyncSummary *out);
```

**`client/panel.c`**: `SyncEntry`/`SyncIndex`/Index-Aufbau/`SyncAction(s)`/`queue_transfer_by_mtime()` unverändert als `static` übernehmen; `do_sync()`s Körper wird zu `panel_sync()`:
- Kein `ShellSession`/Argument-Parsing mehr (Aufrufer übergibt fertige `local_root`/`remote_root`/`dry_run`).
- Jedes `printf`/`fprintf(stderr)` wird zu `ui_notify(UI_INFO/UI_WARN, ...)`, gated durch `if (verbose)` — außer der finalen Zusammenfassungszeile, die immer ausgegeben wird (analog zum immer aktiven `"Uploaded"/"Downloaded"` in `cmd.c`).
- `eff_config()`/`ShellSession` entfällt komplett — verifiziert: alle intern gebauten Remote-Pfade sind bereits absolut (`/`-präfigiert), `cfg->basepath` spielt für `panel_sync()` keine Rolle.
- `Progress` wird vom Aufrufer übergeben statt lokal angelegt (damit ein GUI später live mitlesen könnte — v1 verzichtet bewusst darauf, siehe unten).

**`client/shell.c`**: `do_sync()` wird zum dünnen Wrapper (Flag-/Pfad-Parsing bleibt hier, da `ShellSession`-spezifisch), ruft `panel_sync(&eff, local_root, remote_root, dry_run, /*verbose=*/1, &pr, &sum)`. Konsolen-Ausgabe bleibt unverändert, da `console_notify()` denselben stdout/stderr-Split reproduziert wie die bisherigen Rohaufrufe.

**`client/gui_amiga/gui.c`** — neues `do_sync_menu()`:
1. Prüfen, dass `g_left`/`g_right` unterschiedliche Sources haben (eine lokal, eine Cloud) — sonst `MUI_Request`-Fehlermeldung.
2. Trockenlauf (`dry_run=1, verbose=0`) synchron ausführen, Ergebnis-Zusammenfassung per `MUI_Request`/Ja-Nein bestätigen lassen.
3. Bei Bestätigung: echter Lauf (`dry_run=0`), danach beide Panels neu laden.

**`client/gui_qt/`**: `TransferJob` um `Op::SyncPanels` erweitern (nutzt die vorhandene Hook-Bridging-Infrastruktur mit, statt eine Parallelklasse zu bauen); `MainWindow::doSync()` mit derselben Trockenlauf→Bestätigung→Echtlauf-Logik, neues Menü `&Extras` (zusätzlich zum bestehenden `&Ansicht`) mit Eintrag "Panels synchronisieren…" (**kein Button**, wie gewünscht).

**Bewusste Scope-Entscheidung (nicht extra abgefragt, hier dokumentiert):** Der Sync-Fortschritt zeigt in v1 nur Fortschritt der einzelnen laufenden Datei, keine Gesamt-x/y-Blend wie bei normalen Kopieroperationen — die zugrunde liegende Sync-Engine ist laut Nutzer selbst noch ungetestet, ein einfacherer erster Wurf ist hier angemessener als zusätzliche Komplexität.

## 6. "Prüfsumme anzeigen" (Qt + Amiga)

Nutzt vorhandene Bausteine: `PanelEntry.md5` ist für Cloud-Einträge bereits befüllt; für lokale Einträge wird `md5_file_hex()` (`client/md5.h`) on-demand gerufen.

- **Amiga** (`gui.c`): neues `do_checksum()`, iteriert die Selektion via `panel_mui_next_selected()` (wie `do_copy()` es bereits tut), zeigt bis zu 20 Zeilen `Name: <md5|Verzeichnis|Fehler>` per `MUI_Request`, darüber hinaus "… und N weitere".
- **Qt** (`mainwindow.cpp`): neues `MainWindow::doChecksum()`, nutzt `PanelWidget::targetEntries()`/`childPath()`, zeigt Ergebnis per `QMessageBox::information`. Menüeintrag im neuen `&Extras`-Menü (zusammen mit Sync).

## 7. Amiga: F-Tasten-Shortcuts reparieren + aktives Panel visuell markieren

**F-Tasten funktionieren nicht.** Root Cause in `client/gui_amiga/gui.c` (Zeile ~622–628): die Buttons werden per `SimpleButton("_F2 Neu laden")` usw. angelegt. `SimpleButton(label)` ist `MUI_MakeObject(MUIO_Button, label)` — das `_` vor einem Zeichen setzt `MUIA_ControlChar` auf genau dieses eine Zeichen, hier bei **allen sieben** Buttons (`_F2`, `_F3`, `_F5`, `_F6`, `_F7`, `_F8`, `_F10`) auf denselben Buchstaben `'F'`. Das kollidiert also nicht nur untereinander, sondern reagiert ohnehin nur auf Amiga+F (Tastatur-Qualifier+Buchstabe), niemals auf die echten F-Tasten — `MUIA_ControlChar` kennt keine Funktionstasten.

Fix: nicht auf `MUIA_ControlChar` verlassen, sondern echte Rohtastatur-Events abfangen. Übliches MUI-Muster: `DoMethod(window, MUIM_Notify, MUIA_Window_InputEvent, MUIV_EveryTime, ...)` mit einem Hook, der `IDCMP_RAWKEY`-Nachrichten prüft und deren `Code` gegen die Amiga-Rohcodes für F1–F10 (0x50–0x59) abgleicht, und dann dieselben bestehenden Aktionsfunktionen aufruft, die die Buttons schon nutzen (`do_reload()`, `do_info()`, `do_copy()`, `do_renmove()`/Umbenennen-Verschieben, `do_mkdir()`, `do_delete()`, `ID_QUIT`) — keine neue Fachlogik, nur ein zweiter, echter Eingabepfad neben der bestehenden Button-`MUIM_Notify`-Verdrahtung. Button-Beschriftungen bleiben als Hinweistext ("F2 Neu laden" ohne das führende `_`, da der Buchstaben-Shortcut ohnehin nie korrekt funktioniert hat); ob ein Amiga+Buchstabe-Shortcut zusätzlich erhalten bleibt, ist ein kleiner Folge-Entscheid, die Kollision auf `'F'` muss so oder so weg.

**Aktives Panel ist visuell nicht erkennbar.** `set_active()` (`gui.c:299–306`) ändert aktuell nur eine flüchtige Statuszeile ("Aktiv: links"/"Aktiv: rechts") — der bestehende Code-Kommentar dort dokumentiert das bereits explizit als bewusste v1-Vereinfachung ("No per-panel visual highlight in v1 … status line … cheap and unambiguous"). Der Nutzer bestätigt, dass das in der Praxis übersehen wird.

Fix: `AmigaPanel.group` (`gui.h:36`, die äußere VGroup jedes Panels, bereits als Feld vorhanden) bekommt in `set_active()` einen dauerhaften visuellen Marker statt/zusätzlich zur Statuszeile — z. B. `MUIA_Frame` zwischen normalem und hervorgehobenem Rahmen umschalten (`SetAttrs(p->group, MUIA_Frame, ..., TAG_DONE)`), oder den Gruppentitel um ein Präfix wie „▶ “ ergänzen. Bei jedem `set_active()`-Aufruf werden beide Seiten aktualisiert: aktives Panel markiert, das andere zurückgesetzt.

Beide Punkte betreffen ausschließlich `client/gui_amiga/gui.c` und sind — wie der Rest dieses Verzeichnisses — bislang nicht auf echter/emulierter Hardware getestet; besonders der Raw-Key-Hook verdient dort einen genauen Blick.

## Kritische Dateien

- `client/panel.h`, `client/panel.c` — `Progress.cur_file_size`, `panel_sync()`+Sync-Typen
- `client/ui_hooks.h`, `client/ui_hooks.c` — `UiRateMeter`, echte Konsolen-Fortschrittszeile
- `client/http_amiga.c` — `ssl_read_all()` Content-Length-Parsing
- `client/gui_amiga/gui.c` — Menü, About, Sync-Menüpunkt, Prüfsumme-Menüpunkt, Byte-Gauge, bps, F-Tasten-Rawkey-Hook, aktives-Panel-Markierung
- `client/gui_qt/transferjob.h`/`.cpp`, `client/gui_qt/mainwindow.cpp`/`.h` — Byte-Signal, bps, `&Extras`-Menü mit Sync + Prüfsumme
- `client/shell.c` — `do_sync()` zum Wrapper um `panel_sync()` verschlankt

## Verifikation

- **PC-Build (Qt)**: `cmake --build` im `gui_qt`-Build-Verzeichnis; App starten, einen echten Up-/Download gegen den Server auslösen und beobachten, dass der Fortschrittsbalken sich flüssig bewegt und eine bps-Zahl anzeigt (nicht nur Datei-Sprünge). Sync-Menüpunkt gegen ein Testverzeichnis mit bewusst unterschiedlichem Stand links/rechts ausprobieren (inkl. Trockenlauf-Vorschau), Prüfsumme-Menüpunkt für eine lokale und eine Cloud-Datei testen.
- **CLI/Shell**: `amicloud get`/`put` bzw. `amicloud-shell` mit einer größeren Testdatei laufen lassen, prüfen dass eine `\r`-Statuszeile mit %/bps erscheint und nach Abschluss sauber von der Erfolgsmeldung abgelöst wird.
- **Amiga**: da `client/gui_amiga/` laut vorhandenem Code-Kommentar bislang **nie** auf echter Hardware/Emulator übersetzt oder getestet wurde, sollte nach dem Cross-Build (`vc +aos68k`) ein Testlauf in einem Amiga-Emulator (z. B. FS-UAE/WinUAE) erfolgen: Menü öffnen (About, Sync, Prüfsumme, Beenden), Up-/Download beobachten (Gauge-Bewegung + bps-Text), Sync einmal gegen ein kleines Testverzeichnis fahren, jede F-Taste (F2/F3/F5/F6/F7/F8/F10) einzeln drücken und prüfen, dass sie dieselbe Aktion wie der zugehörige Button auslöst, und per Klick zwischen linkem/rechtem Panel wechseln um zu prüfen, dass die aktive Seite auch ohne Blick auf die Statuszeile erkennbar ist. Der `http_amiga.c`-Content-Length-Fix und der Raw-Key-Hook sind die riskantesten Teile dieses Plans und verdienen dort besondere Aufmerksamkeit (HLOG-Traces prüfen, dass `total_body` korrekt erkannt wird; Raw-Key-Codes gegen ein echtes deutsches Amiga-Keymap verifizieren).
