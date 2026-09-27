# AmiCloud — Suchfunktion (`search` / `find`)

Stand: 2026-07-15 · Nur Planung — Umsetzung folgt nach Review · HTML-Fassung: `search-plan.html`

## Context

AmiCloud hat bisher keinen Weg, im Cloud-Baum nach Dateien/Ordnern zu suchen
— man kann nur Verzeichnis für Verzeichnis mit `ls`/Panel navigieren. Diese
Änderung ergänzt einen **`search`- (Alias `find`) Befehl** in allen drei
Front-Ends (CLI `amicloud`, `amicloud-shell`, GUI `amicloud-commander`), der
mit den klassischen Jokern **`*`, `?` und AmigaDOS `#?`** nach Datei- oder
Ordnernamen sucht.

Zwei per Rückfrage bestätigte Design-Entscheidungen:

1. **Server-seitige Suche** — ein neuer `api.php?action=search`-Endpunkt
   fragt die `files`-DB in **einem** Request ab (blitzschnell, auch bei
   großen Bäumen), statt clientseitig Verzeichnis für Verzeichnis zu laufen.
2. **Standard-Scope = aktuelles Verzeichnis rekursiv** (Shell-cwd bzw.
   aktives Commander-Panel), mit Option „ganze Cloud ab `/`".

Damit braucht **kein** Client-Makefile eine neue Lib: der Server macht das
Matching, die Clients rufen nur `cmd_search()` in libcloudhttp auf. Der
existierende `changes()`-Endpunkt ist die fast wörtliche Vorlage; der
Wildcard-Matcher `pattern.h` (libcloudfs) wird nur im Commander für die
**lokale** Panel-Seite gebraucht.

---

## 1. Server: neuer `action=search`

### `server/api.php`
Neuen `case` in den Dispatch-`switch` (nach `case 'changes'`, ~Zeile 35):
```php
case 'search':   FileStore::search($path, trim($_GET['q'] ?? ''),
                     isset($_GET['dirs']) ? (bool)$_GET['dirs'] : true); break;
```
`$path` ist der Scope-Root (schon via `sanitizePath` geholt); `q` ist das
Wildcard-Muster.

### `server/includes/FileStore.php`
Nach `changes()` (Zeile 501–532) analog aufbauen — dieselbe SELECT-Form,
dieselbe Envelope-Struktur (`entries`-Array mit `/`-präfixierten Pfaden):

```php
/* Glob (*,?,#?) -> SQL-LIKE. Reihenfolge wichtig: #? vor ? behandeln.
   %,_,\ im Literal-Text werden via likeEscape() maskiert. */
private static function globToLike(string $glob): string { ... }

public static function search(string $path, string $glob, bool $dirsToo): void
{
    if ($glob === '') { Response::error(400, 'search requires q'); return; }
    $scope = dbPath($path);                    // relativ, ohne führenden '/'
    $like  = self::globToLike($glob);
    $sql = 'SELECT id, path, is_dir, size_bytes, checksum, revision,
                   change_seq, UNIX_TIMESTAMP(modified_at) AS mtime_ts
            FROM files
            WHERE user_id = ? AND deleted_at IS NULL
              AND SUBSTRING_INDEX(path, "/", -1) LIKE ? ESCAPE "\\\\" ';
    $args = [Auth::$user_id, $like];
    if ($scope !== '') {                        // nur unterhalb des Scopes
        $sql   .= 'AND path LIKE ? ESCAPE "\\\\" ';
        $args[] = self::likeEscape($scope) . '/%';
    }
    if (!$dirsToo) { $sql .= 'AND is_dir = 0 '; }
    $sql .= 'ORDER BY path ASC LIMIT 2000';     // Sicherheitskappe
    // fetchAll -> entries[] wie in changes(): path => '/'.$row['path'],
    //   type file|dir, size, md5, mtime, id, revision, change_seq
    // Response::ok(['entries'=>$entries, 'truncated'=> count==2000]);
}
```

Wichtig (Repo-Memory): **PHP 7.4-kompatibel** bleiben (kein `match`, keine
enums/union types). Prepared Statements + `likeEscape()` wie überall sonst.
Matching auf dem **Basename** (`SUBSTRING_INDEX(path,'/',-1)`), da die Joker
per `pattern.h`-Konvention nur das letzte Pfadsegment betreffen.

Grenze: reine AmigaDOS-Klassen (`[abc]`, `(a|b)`) übersetzt `globToLike`
nicht 1:1 — für `*`/`?`/`#?` (der plattformübergreifende Standard, den der
Nutzer nennt) ist die Abbildung exakt. Das reicht für die Anforderung.

### `docs/api/API.md`
`action=search` dokumentieren (Parameter `path`, `q`, `dirs`; Envelope).

---

## 2. Client-Kern: `cmd_search()` in libcloudhttp

### `core/libcloudcore/include/cloudcore/entry.h`
Neuer Trefferty­p neben `AcChange`:
```c
typedef struct {
    char path[1024];   /* voller '/'-Pfad, wie AcChange.path */
    int  is_dir;
    long size;
    long mtime;
} AcSearchHit;
```

### `core/libcloudhttp/include/cloudhttp/cmd.h` + `src/cmd.c`
Neue Funktion, modelliert nach `cmd_changes()` (cmd.c:371–430):
```c
/* GET api.php?action=search&path=<root>&q=<glob>. Trefferliste als
   malloc'd AcSearchHit[], mit cmd_search_free() freigeben. 1 = ok. */
int  cmd_search(const AcConfig *cfg, const char *root, const char *pattern,
                AcSearchHit **out, int *out_n);
void cmd_search_free(AcSearchHit *hits);
```
Nutzt vorhandenes `build_url()`/`url_encode()` (cmd.c:31,68) für den
`&q=`-Parameter, `check_status()` + parson zum Parsen der `entries`.
Rückgabe der Server-`truncated`-Flag optional per zusätzlichem `int*`.

---

## 3. CLI `amicloud`

### `apps/amicloud/src/main.c`
Neuen Zweig in die `if/else if`-Kette (nach `rename`, ~Zeile 118):
```c
} else if (strcmp(cmd,"search")==0 || strcmp(cmd,"find")==0) {
    if (i >= argc) { fprintf(stderr,"amicloud: search requires <pattern>\n"); rc=1; }
    else {
        const char *pat  = argv[i++];
        const char *root = (i < argc) ? argv[i] : NULL; /* NULL = basepath/"/" */
        rc = cmd_search_print(&cfg, root, pat);         /* dünner Drucker um cmd_search */
    }
}
```
`cmd_search_print` (in cmd.c oder main.c): ruft `cmd_search`, druckt jeden
Treffer als vollen Pfad je Zeile (Verzeichnisse mit `/`-Suffix).

### `core/libcloudcore/src/config.c`
`config_usage()` (Zeile 116–141): Zeile `  search <pattern> [path]  ...` ergänzen.
**Kein Makefile-Change** (nur libcloudhttp nötig, schon gelinkt).

---

## 4. `amicloud-shell`

### `apps/amicloud-shell/src/shell.c`
- `dispatch()` (Zeile 741+): `if (ac_strcasecmp(cmd,"search")==0 ||
  ac_strcasecmp(cmd,"find")==0) { do_search(s, tok+1, n-1); return 1; }`
- Neues `do_search()`: `pattern = tok[0]`, optionaler Scope `tok[1]` (sonst
  Standard = cwd). Nutzt `eff_config()` (shell.c:181) damit der Scope relativ
  zum aktuellen `remote_cwd` aufgelöst wird → `cmd_search(&eff, scope, pat,…)`,
  Treffer zeilenweise drucken.
- `print_help()` (Zeile 233): `search`/`find` in die Hilfe aufnehmen (~70 Sp.).
**Kein Makefile-Change** (libcloudhttp schon gelinkt).

Beispiele:
```
amicloud search '*.txt' /            # ganze Cloud
> cd docs
> search #?.info                     # cwd-relativ, rekursiv
> find foo?
```

---

## 5. `amicloud-commander` (der aufwändige Teil)

Aktives Panel entscheidet die Quelle: **PANEL_CLOUD → `cmd_search`** (Server),
**PANEL_LOCAL → lokaler Walk** (`localfs_readdir` + `pattern_match`). Für die
lokale Seite kommt der Wildcard-Matcher `pattern.h` ins Spiel.

### Geteilter Kern — `core/common/{include/common/search.h,src/search.c}` (neu)
```c
typedef struct { char path[1024]; int is_dir; long size, mtime; } SearchHit;
typedef void (*SearchHitFn)(const SearchHit*, void *ud);
/* lokaler rekursiver Walk: localfs_opendir/readdir (Vorlage:
   count_local_tree(), panel.c:135) + pattern_match() auf Basenamen. */
int search_local(const char *root, const char *pattern, SearchHitFn cb, void *ud);
```
(Cloud-Suche im Commander ruft direkt `cmd_search`; `search_local` deckt die
lokale Panel-Seite ab. Vorlage-Rekursion existiert in `panel.c`.)

### Ablauf
- **Requester** — schöner Such-Dialog: Muster-Eingabe, Scope (aktueller
  Ordner / ganze Cloud), Checkbox „auch Ordner".
- **Ergebnisfenster** — *modelos*, listet alle Treffer als volle Pfade.
  **Einzelinstanz: wird bei neuer Suche wiederverwendet** (neu befüllen +
  nach vorn), nicht ein zweites Fenster.
- **Gehe zu** — Doppelklick/Enter auf einen Treffer: das *auslösende* Panel
  springt in den Ordner und markiert die Datei.

### Qt (`apps/amicloud-commander-qt/`)
Neue Dateien:
- **`src/searchdialog.{h,cpp}`** — „schöner Requester": `QLineEdit`
  Suchmuster, Radiobuttons *aktueller Ordner* / *ganze Cloud (/)*, Checkbox
  „auch Ordner". Modaler `QDialog` (Vorlage: `connectdialog.cpp`,
  `QFormLayout`+`QDialogButtonBox`). Liefert Muster + Scope zurück.
- **`src/searchresultswindow.{h,cpp}`** — **modeloses** Ergebnisfenster
  (`QDialog`/`QWidget`, kein `exec()`): eigener `QTableView` über ein neues
  `SearchResultsModel : QAbstractTableModel` über `QVector<SearchHit>`
  (Spalten Pfad/Größe/Datum). Signal `activated(SearchHit)` bei Doppelklick/
  Enter/„Gehe zu"-Button. (Vorlage-Shape: `paneltablemodel.cpp`,
  `logtablemodel` in `amivcs-gui-qt`.)

Änderungen:
- **`src/mainwindow.{h,cpp}`**
  - `void doSearch();` Slot: Requester zeigen → auf aktivem Panel suchen
    (Cloud: `cmd_search`; Local: `search_local`) → **Einzelinstanz**
    `SearchResultsWindow *m_searchResults=nullptr`: existiert es, `setResults()`
    + `raise()`/`activateWindow()`; sonst neu bauen, `activated`-Signal an
    `gotoSearchHit()` binden, `destroyed`→Pointer nullen. **Ergebnisfenster
    wird wiederverwendet.**
  - Merkt sich das **auslösende Panel** (`m_active` zum Suchzeitpunkt).
  - `void gotoSearchHit(const SearchHit&)`: Pfad in Ordner+Basename splitten,
    `launchPanel->revealEntry(dir, name)`, Hauptfenster nach vorn.
  - Einbinden über die drei Registrierungspunkte: `addButton(tr("F4 Suchen"),
    &MainWindow::doSearch)` (mainwindow.cpp:62), Menüpunkt (Extras-Menü, :91),
    `keyPressEvent`-`case Qt::Key_F4:` (F4/F9 frei, :134).
- **`src/panelwidget.{h,cpp}`** — neue Methode
  `void revealEntry(const QString &dir, const QString &name)`:
  `navigateTo(dir)` (lädt synchron via `reload()`/`panel_read`), dann Zeile
  über neue Modell-Hilfe finden, `m_table->setCurrentIndex(...)` +
  `scrollTo(...)`, Zeile markieren.
- **`src/paneltablemodel.{h,cpp}`** — `int rowForName(const QString&) const`
  (findet Datei **oder** Ordner; `entryByName()` matcht heute nur Nicht-Dirs).
- **`CMakeLists.txt`** — Quellen ergänzen: `libcloudfs/src/pattern_pc.c`
  (bislang nicht kompiliert!), `common/src/search.c`, und die neuen
  `searchdialog.cpp`/`searchresultswindow.cpp`.

### MUI (`apps/amicloud-commander-mui/`) — UNVERIFIED, spiegelbildlich
- **`src/gui.c`**: `#define ID_SEARCH` (ID-Block ~:38); `do_search()`:
  `ask_string("Suchmuster:", …)` (vorhandener Requester, gui.c:305) für das
  Muster; Suche auf aktivem Panel (Cloud `cmd_search` / Local `search_local`).
  Ergebnisfenster als **globale Einzelinstanz** `g_search_window` mit
  `ListObject`+`MUIA_List_DisplayHook` (Vorlage:
  `amivcs-gui-mui/src/historylist.c`); offen → `MUIM_List_Clear`+neu füllen+
  `MUIM_Window_ToFront` (**wiederverwenden**). Go-to bei Doppelklick/Return:
  Pfad splitten → `panel_mui_set_cloud/set_local(g_active, dir)` + reload +
  `set(list_obj, MUIA_List_Active, idx)` zum Markieren. Registrierung:
  `NM_ITEM` in `MenuData[]` (:739), Toolbar-Button (:828), F-Key-Notify
  (`"-f4"`, :878), `case ID_SEARCH:` im `switch(id)` (:928).
- **`Makefile.amiga-cross`**: `search.o` (aus common) ergänzen
  (`pattern_amiga.o` ist bereits dabei).

---

## 6. Doku
- `docs/api/API.md` — `action=search`.
- `docs/user/AmiCloud-Handbuch.html` **und** `docs/user/AmiCloud.guide` —
  `search`/`find` (CLI, Shell) + Commander-Suchfenster beschreiben.

---

## Wiederverwendete Bausteine (nicht neu bauen)
- `cmd_changes()` / `changes()` — fast wörtliche Vorlage für Endpoint+Client.
- `build_url()`, `url_encode()`, `check_status()`, parson — in cmd.c vorhanden.
- `likeEscape()`, `dbPath()`, `Response::ok/error` — Server-Helfer da.
- `pattern_compile/match/free` (libcloudfs) — nur für lokale Commander-Suche.
- `count_local_tree()` (panel.c:135) — Rekursions-Vorlage für `search_local`.
- `eff_config()` (shell.c:181) — cwd-relativer Scope in der Shell.
- `ask_string()` (MUI), `ConnectDialog`/`QFormLayout` (Qt) — Requester-Muster.
- `PanelTableModel`/`entryAt` bzw. `historylist.c`-DisplayHook — Ergebnisliste.

## Zu ändernde Dateien

| Datei | Art | Was |
|---|---|---|
| `server/api.php` | edit | Dispatch-`case 'search'` |
| `server/includes/FileStore.php` | edit | `search()` + `globToLike()` |
| `core/libcloudcore/include/cloudcore/entry.h` | edit | `AcSearchHit` |
| `core/libcloudhttp/include/cloudhttp/cmd.h`, `src/cmd.c` | edit | `cmd_search()` / `cmd_search_free()` |
| `apps/amicloud/src/main.c` | edit | `search`/`find`-Zweig |
| `core/libcloudcore/src/config.c` | edit | Usage-Zeile |
| `apps/amicloud-shell/src/shell.c` | edit | Dispatch + `do_search` + Hilfe |
| `core/common/include/common/search.h`, `src/search.c` | neu | `search_local()` (Commander-Local) |
| `apps/amicloud-commander-qt/src/searchdialog.{h,cpp}` | neu | Such-Requester |
| `apps/amicloud-commander-qt/src/searchresultswindow.{h,cpp}` | neu | Ergebnisfenster + Modell |
| `apps/amicloud-commander-qt/src/{mainwindow,panelwidget,paneltablemodel}.*` | edit | doSearch/Go-to/reveal/rowForName |
| `apps/amicloud-commander-qt/CMakeLists.txt` | edit | + pattern_pc.c, search.c, neue .cpp |
| `apps/amicloud-commander-mui/src/gui.c` | edit | ID_SEARCH, do_search, Ergebnisfenster |
| `apps/amicloud-commander-mui/Makefile.amiga-cross` | edit | + search.o |
| `docs/api/API.md`, `docs/user/AmiCloud.*` | edit | Endpoint + Befehl dokumentieren |

## Verifikation
1. **Server**: `globToLike()` isoliert testen (Beispiele `*.txt`, `#?.info`,
   `foo?`, Literal mit `%`). Endpoint per curl gegen Test-DB:
   `curl '…/api.php?action=search&path=/&q=*.txt' -u user:pass` → JSON-`entries`
   prüfen; Scope (`path=/docs`) und `dirs=0` prüfen. PHP 7.4-Lint
   (`php -l`, keine `match`/enums).
2. **CLI/Shell (PC)**: `make -f Makefile.pc` für amicloud + amicloud-shell;
   `amicloud search '*.txt' /`, in der Shell `cd docs` + `search #?.info`
   (cwd-relativ) und `find foo?` — Trefferpfade gegen bekannten Baum prüfen.
3. **Commander (Qt)**: `cmake --build build-qt`; App starten (`/run`), F4/
   Suchbutton → Requester, Muster eingeben, Ergebnisfenster prüfen;
   Doppelklick auf Treffer → korrektes Panel springt in den Ordner und
   markiert die Datei; zweite Suche → **dasselbe** Fenster wird wiederbenutzt;
   Cloud- **und** Local-Panel testen.
4. **Amiga cross-compile** (Toolchain `$VBCC`, s. docs/BUILD.md):
   libs + amicloud + amicloud-shell + commander-mui kompilieren/linken sauber
   (`Makefile.amiga-cross`). MUI-Laufzeit bleibt UNVERIFIED (keine Hardware).
