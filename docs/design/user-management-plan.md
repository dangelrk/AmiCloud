# Plan: Benutzerverwaltung & Weboberfläche im ownCloud-Stil

Stand: 2026-10-22 · Status: **freigegeben** (Review Runde 2, Auflagen in §12)

> §12 hat Vorrang vor widersprechenden Stellen in §3–§9.

## 1. Ziel

AmiCloud wird vom „Einbenutzer-Server mit SQL-angelegtem Benutzer“ zu einem
Mehrbenutzer-Dienst, der sich im Browser wie ownCloud/Nextcloud bedient:

- Benutzer werden **in der Weboberfläche** verwaltet (Admin), nicht mehr per SQL.
- Jeder Benutzer hat **persönliche Einstellungen**: Anzeigename, E-Mail,
  Passwort ändern, **Geräte-/App-Passwörter** für die Clients, Speicherplatz.
- **Speicherkontingent (Quota)** pro Benutzer, sichtbar in Web, CLI, Shell,
  Commander und als echte Laufwerksbelegung von `CLOUD:` (`Info`).
- **Öffentliche Links** (Freigabe einer Datei oder eines Ordners, optional mit
  Passwort und Ablaufdatum) – das Kernmerkmal von ownCloud.
- Die Dateiansicht bekommt das ownCloud-Bedienkonzept: Kopfleiste mit Suche und
  Benutzermenü, Navigationsleiste links mit Quota-Balken, „+ Neu“-Menü,
  Drag & Drop-Upload mit Fortschritt, „…“-Menü pro Zeile, Auswahlleiste,
  Details-Seitenleiste.

Bestehende Daten/Benutzer werden **nicht** migriert (nur Testdaten). Das Schema
wird trotzdem additiv per `migrate_004.sql` erweitert, damit andere
Installationen sauber nachziehen können.

### Nicht Bestandteil

- Gruppen, gemeinsame Ordner zwischen Benutzern, ACLs (die API bleibt
  benutzerneutral; ADR 04 wird durch ADR 05 fortgeschrieben, s. §12.10).
- Papierkorb/Versionen (Dateien werden auf der Platte sofort gelöscht; der
  Sync arbeitet mit Tombstones – ein Papierkorb würde dessen Semantik ändern).
- Vorschaubilder/Viewer, Kalender/Kontakte, LDAP, 2FA, E-Mail-Versand.
- Benutzerverwaltung über die REST-API (bleibt Web + CLI; die Clients brauchen
  sie nicht).

## 2. Ist-Zustand (relevant)

| Bereich | Heute |
|---|---|
| `users` | `id, username, pass_hash, created_at, active, global_revision` |
| Anlegen | `INSERT` von Hand (docs/INSTALL.md §6) |
| Speicherort | `STORAGE_PATH/<username>` (`storagePath()` in FileStore.php) |
| API-Auth | Basic-Auth gegen `pass_hash` (bcrypt) bei **jedem** Request |
| Web-Auth | Session; `user_id`/`username` nur beim Login aus der DB gelesen |
| `status` | `version, time, global_revision` |
| Handler `Info` | Platzhalter (`NumBlocks=0x3FFFFF`, halb belegt) |
| Web-UI | eine Seite (Tabelle, Upload, Ordner anlegen, Umbenennen, Löschen) |

## 3. Datenmodell (`sql/schema.sql` + `sql/migrate_004.sql`)

```sql
ALTER TABLE users
  ADD COLUMN display_name VARCHAR(128) NOT NULL DEFAULT '',
  ADD COLUMN email        VARCHAR(255) NOT NULL DEFAULT '',
  ADD COLUMN is_admin     TINYINT(1)   NOT NULL DEFAULT 0,
  ADD COLUMN quota_bytes  BIGINT UNSIGNED NULL DEFAULT NULL,  -- NULL = unbegrenzt
  ADD COLUMN last_login   DATETIME NULL DEFAULT NULL;

CREATE TABLE app_passwords (            -- „Geräte & Sitzungen“
  id           INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  user_id      INT UNSIGNED NOT NULL,
  name         VARCHAR(64)  NOT NULL,   -- z. B. „A1200 CLOUD:“
  token_hash   CHAR(64)     NOT NULL UNIQUE,  -- SHA-256 des Tokens
  created_at   DATETIME DEFAULT CURRENT_TIMESTAMP,
  last_used_at DATETIME NULL,
  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE);

CREATE TABLE shares (                   -- öffentliche Links
  id         INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  user_id    INT UNSIGNED NOT NULL,
  path       VARCHAR(1024) NOT NULL,    -- wie files.path (ohne führenden /)
  token      CHAR(32) NOT NULL UNIQUE,  -- bin2hex(random_bytes(16))
  pass_hash  VARCHAR(255) NULL,         -- optionales Link-Passwort (bcrypt)
  expires_at DATE NULL,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  downloads  INT UNSIGNED NOT NULL DEFAULT 0,
  KEY idx_user_path (user_id, path(255)),
  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE);

CREATE TABLE auth_failures (            -- Brute-Force-Bremse
  id       BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  ip       VARCHAR(45) NOT NULL,
  username VARCHAR(64) NOT NULL,
  at       DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY idx_ip_at (ip, at));
```

Entscheidungen:

- **Benutzername ist unveränderlich** (wie ownCloud-UID). Der Speicherort
  `STORAGE_PATH/<username>` bleibt deshalb unverändert; änderbar ist nur der
  Anzeigename. Erlaubte Zeichen beim Anlegen: `[A-Za-z0-9._-]{1,64}`, nicht
  `.`/`..` (Pfadsicherheit).
- **App-Passwörter** sind zufällige Tokens (`xxxxx-xxxxx-xxxxx-xxxxx-xxxxx`,
  25 Zeichen aus `[a-z2-9]` ohne Verwechsler) und werden als **SHA-256**
  gespeichert, nicht bcrypt: Bei 128 bit Entropie bringt bcrypt nichts, kostet
  aber bei jedem Amiga-Request ~50–100 ms. Lookup per `token_hash` (Index).
  Angezeigt wird das Token **nur einmal** beim Anlegen.
- **Quota-Belegung** = `SUM(size_bytes)` der lebenden Dateizeilen
  (`deleted_at IS NULL AND is_dir = 0`). Voraussetzung: der Web-Upload wird
  auf denselben Schreibweg wie die API umgestellt (§12.2). Billig genug für
  jeden `status`-Aufruf.
- `auth_failures` wird bei jedem Login-Versuch auf > 1 Tag alte Zeilen
  bereinigt (kein Cron nötig).

## 4. Server – Code-Struktur

Neue/geänderte Dateien unter `server/`:

| Datei | Inhalt |
|---|---|
| `includes/Users.php` | Benutzer-Modell: `find/byName/all/create/update/setPassword/delete`, `usedBytes()`, `quotaOk($delta)`, Validierung Benutzername/Passwort (min. 8 Zeichen), Schutz „letzter aktiver Admin“ |
| `includes/AppPasswords.php` | `create(name) → Klartext-Token`, `listFor(uid)`, `revoke(id)`, `verify(user, token)` |
| `includes/Shares.php` | `create(path, pass, expires)`, `listFor(uid)`, `forPath`, `byToken`, `delete`, `onRename(from,to)`, `onDelete(path)` |
| `includes/Throttle.php` | `check(ip, user)`: ab 5 Fehlversuchen in 15 min → 429 bzw. Web-Hinweis; `fail()`, `clear()` |
| `includes/Auth.php` | Basic-Auth: Passwort **oder** App-Passwort, Throttle, setzt zusätzlich `Auth::$is_admin`; neue `Auth::loadSessionUser()` für das Web: liest den Benutzer bei **jedem** Request aus der DB (gelöschter/deaktivierter Benutzer → Session ungültig; Admin-Recht nicht aus der Session) |
| `includes/FileStore.php` | Quota-Prüfung in `upload()`/`uploadRaw()` (Delta = neue Größe − alte Größe); `renameImpl()`/`deleteImpl()` rufen `Shares::onRename/onDelete`; neue `recent($limit)` (zuletzt geänderte Dateien aus der DB) |
| `includes/web/*.php` | Views statt einer 636-Zeilen-Datei: `layout.php` (Kopfleiste, Navigation, Quota-Balken), `files.php`, `settings.php`, `users.php`, `shares.php`, `login.php`, `icons.php` (Inline-SVG-Icons nach Dateityp) |
| `web.php` | wird zum Controller: Routing über `?view=files|recent|search|shares|settings|users` und POST-Aktionen; Login/Logout/CSRF wie bisher |
| `share.php` | öffentliche Linkseite (ohne Login): Datei-Download bzw. Ordner-Ansicht + Einzel-Download + „Alles als ZIP“ (ext-zip ist auf dem Server vorhanden), Passwortabfrage, Ablauf → 404 |
| `amicloud-admin.php` | CLI-Werkzeug nach Art von `occ` (`PHP_SAPI === 'cli'`, sonst 403): `user:list`, `user:add [--admin] [--quota=5G] <name>`, `user:passwd <name>`, `user:admin <name> on|off`, `user:disable/enable`, `user:delete`. Passwort interaktiv oder `--password-stdin`; per `.htaccess` für den Browser gesperrt |
| `api.php` | `status` erweitert, Quota-Fehler 507 |
| `web.css` | Neues Layout (siehe §6); Farben/Variablen bleiben |

PHP 7.4 bleibt Pflicht (kein `match`, keine Union-Types, keine Enums).

### Erster Admin

- Neuinstallation: `php amicloud-admin.php user:add --admin <name>`
  (docs/INSTALL.md §6 ersetzt das SQL-`INSERT`).
- **Kein** Web-Installer: Ein „erstes Konto anlegen“-Formular wäre auf einer
  frisch deployten, öffentlich erreichbaren Instanz ein Wettlauf.

## 5. REST-API (abwärtskompatibel, additiv)

- **Auth:** Basic-Auth akzeptiert das Kontopasswort **oder** ein App-Passwort
  (gleicher Benutzername). Nichts an den Clients ändert sich; empfohlen wird
  künftig ein App-Passwort in `amicloud.cfg`. Deaktivierte Benutzer → 401.
  Zu viele Fehlversuche → **429** `Too many failed logins`.
- **`status`** liefert zusätzlich:
  ```json
  "user": {"name": "bob", "display_name": "Bob"},
  "quota": {"used": 123456, "total": 5368709120, "free": 5368586664}
  ```
  `total`/`free` = `-1` bei unbegrenztem Kontingent.
- **`upload`**: überschreitet die Datei das Kontingent → **507**
  `Quota exceeded` (vor dem Schreiben, also nichts halb geschrieben).
- Keine neuen Actions; Benutzerverwaltung bleibt außerhalb der API.

## 6. Weboberfläche (ownCloud-Bedienkonzept)

Server-gerendertes PHP + kleines Vanilla-JS wie bisher; kein Framework, kein
Build-Schritt. Gestaltung bleibt im AmiCloud-Farbschema (dunkel, Rot/Türkis,
Boing-Logo).

```
┌──────────────────────────────────────────────────────────────────────┐
│ ◉ AmiCloud        [ 🔍 Dateien durchsuchen…        ]        (R) ▾   │
├──────────────┬───────────────────────────────────────────────────────┤
│ ▸ Alle Dateien│ ⌂ › Projekte › Demo          [+ Neu ▾]               │
│ ▸ Zuletzt     │ ☐ Name ▲                    Größe   Geändert        │
│ ▸ Freigaben   │ ☐ 📁 Disks                    —   vor 2 Std.   ⋯    │
│               │ ☐ 🗜 Workbench.lha        1,2 MB   gestern      ⋯    │
│               │ ☐ 📄 readme.txt             3 KB   12.10.2026   ⋯    │
│               │                                                       │
│ ───────────── │  (Dateien hierher ziehen zum Hochladen)               │
│ ▓▓▓░░░░ 23 %  │                                                       │
│ 1,2 GB von 5 GB│                                                      │
│ ⚙ Einstellungen│                                                      │
└──────────────┴───────────────────────────────────────────────────────┘
```

- **Kopfleiste:** Logo + Name (→ Alle Dateien), Suchfeld (nutzt die
  vorhandene Server-Suche `FileStore::search`, Glob/Teilwort), Benutzermenü
  mit Initialen-Avatar: *Persönliche Einstellungen*, *Benutzer* (nur Admin),
  *Abmelden*.
- **Navigation links:** Alle Dateien · Zuletzt geändert · Freigaben (meine
  Links) · unten Quota-Balken („1,2 GB von 5 GB verwendet“) und
  Einstellungen. Auf schmalen Bildschirmen einklappbar (☰).
- **Dateiliste:** Typ-Icons (Ordner, Bild, Text, Archiv inkl. lha/lzx/adf,
  Audio inkl. mod, Programm, sonstige), Name, Größe, „Geändert“ relativ
  (Tooltip absolut), Sortierung per Spaltenkopf wie bisher, „Ordner zuerst“
  bleibt. Klick auf Ordner öffnet, Klick auf Datei lädt herunter.
- **„…“-Menü pro Zeile:** Herunterladen · Umbenennen · Verschieben (Zielordner
  aus Liste) · Teilen (Link) · Details · Löschen.
- **Auswahlleiste** (erscheint bei ≥ 1 Haken): „3 ausgewählt“ · Verschieben ·
  Als ZIP herunterladen · Löschen.
- **„+ Neu“:** Neuer Ordner · Datei hochladen · Ordner hochladen.
- **Drag & Drop** auf die Liste; Upload per XHR mit Fortschrittsbalken; bei
  Quota-Überschreitung klare Meldung.
- **Details-Seitenleiste** (rechts, aus „…“ → Details): Größe, MD5, Geändert,
  Amiga-Kommentar (editierbar), Protection-Bits (Anzeige `hsparwed`), Freigabe-
  links der Datei mit Anlegen/Löschen/Kopieren.
- **Zuletzt geändert:** die 50 jüngsten Dateien (DB, `modified_at DESC`).
- **Freigaben:** Tabelle aller eigenen Links (Pfad, Link, Passwort ja/nein,
  Ablauf, Downloads, Löschen).
- **Persönliche Einstellungen:** Anzeigename, E-Mail, Passwort ändern
  (aktuelles Passwort nötig), **Geräte & App-Passwörter** (Name eingeben →
  Token einmalig anzeigen + fertige `amicloud.cfg`-Zeilen; Liste mit „zuletzt
  benutzt“, Widerrufen), Speicherplatz-Anzeige.
- **Benutzer (Admin):** Tabelle (Benutzer, Anzeigename, E-Mail, Kontingent mit
  Belegung, Admin, aktiv, letzter Login); Anlegen (Name, Passwort, Anzeigename,
  E-Mail, Kontingent: unbegrenzt/1/5/10 GB/eigener Wert, Admin); Bearbeiten
  (alles außer Name, Passwort zurücksetzen); Deaktivieren; Löschen mit
  Bestätigung durch Eintippen des Namens (löscht DB-Zeilen per CASCADE und den
  Speicherordner). Der letzte aktive Admin kann weder gelöscht, deaktiviert
  noch herabgestuft werden; man kann sich nicht selbst löschen.
- Alle POST-Aktionen weiter mit CSRF-Token; Admin-Aktionen prüfen
  `is_admin` aus der DB.
- `index.php` (Startseite) bleibt; eingeloggte Besucher sehen dort zusätzlich
  „Zu meinen Dateien“.

## 7. Clients

### 7.1 AmiCloudCore (`core/`, gemeinsam mit AmiOwnCloud und AmiVCS)

Nur **additive** Änderung in `libcloudhttp` (AmiCloud-api.php-Schicht,
`cmd.h`/`cmd.c`); `cmd_status()` behält Signatur und Verhalten:

```c
typedef struct {
    char version[32];
    long global_revision;
    long quota_used_kb;   /* belegt, KiB */
    long quota_total_kb;  /* -1 = unbegrenzt; 0 = Server kennt keine Quota */
    char display_name[64];
} AcStatus;

int cmd_status_ex(const AcConfig *cfg, AcStatus *out); /* 1 = ok */
```

- KiB statt Bytes, weil `long` auf dem Amiga 32 bit hat (Bytes > 2 GB
  liefen über); die JSON-Zahl wird als `double` gelesen und durch 1024
  geteilt.
- Alte Server ohne `quota` → `quota_total_kb = 0` (= „unbekannt“).
- Fehlermeldungen: Der Server-Text („Quota exceeded“, „Too many failed
  logins“) kommt über den vorhandenen `message`-Pfad bereits in CLI/Shell an
  – keine Änderung nötig.
- AmiOwnCloud und AmiVCS rufen `cmd_status` nicht anders auf; sie bleiben
  ohne Submodul-Update baubar. Nach der Änderung: PC-Build von **AmiCloud,
  AmiOwnCloud und AmiVCS** gegen den neuen Core prüfen, Amiga-Cross-Compile
  für AmiCloud. Core-Änderung wird in `AmiCloudCore` committet/gepusht, in
  AmiCloud der Submodul-Zeiger angehoben (AmiOwnCloud/AmiVCS optional
  nachziehen).

### 7.2 AmiCloud-Programme

| Programm | Änderung |
|---|---|
| `amicloud` (CLI) | neuer Befehl `quota` (`Belegt: 1,2 MB von 5,0 GB (0 %)` bzw. `unbegrenzt`); `version` zeigt zusätzlich Benutzer + Belegung |
| `amicloud-shell` | Befehl `quota`/`df`; Startbanner zeigt Anzeigename und Belegung |
| `CLOUD:`-Handler | `ACTION_INFO`/`ACTION_DISK_INFO` liefern echte Belegung: neuer Worker-Request `WREQ_STATUS` (über `cmd_status_ex`), Ergebnis im Handler 30 s gecacht; bei unbegrenzt `NumBlocks` = belegt + großzügiger Puffer (z. B. max(2×belegt, 1 GB)), sonst Quota. `BytesPerBlock` = 1024 (KiB passt direkt). `map_put_status`: **507 → ERROR_DISK_FULL**, **429 → ERROR_DISK_NOT_VALIDATED** wie 401 |
| `amicloud-commander` Qt + MUI | Statuszeile eines Cloud-Panels: „N Objekt(e) · 1,2 MB von 5 GB“ (ein `cmd_status_ex` beim Wechsel in die Cloud/Neuladen) |
| `cloudsync` | keine Änderung (Fehlermeldungen kommen durch) |
| `AmiCloudPrefs` | Hinweistext beim Passwortfeld: „App-Passwort aus der Weboberfläche empfohlen“ (nur Text) |

## 8. Dokumentation

- `docs/api/API.md`: Auth mit App-Passwörtern, 429, `status`-Felder, 507.
- `docs/INSTALL.md`: `migrate_004`, `amicloud-admin.php` statt SQL-INSERT,
  Hinweis `AMICLOUD_ALLOW_HTTP` nie in Produktion.
- Handbuch (`docs/user/AmiCloud.guide` Latin-1 + HTML): App-Passwörter,
  `quota`, `Info CLOUD:` zeigt echte Belegung, Weboberfläche/Teilen.
- `README.md`: Komponenten-Tabelle (Server: Weboberfläche mit Benutzerverwaltung,
  Freigaben).

## 9. Umsetzungsreihenfolge

1. Schema + `migrate_004`, Models (`Users`, `AppPasswords`, `Shares`,
   `Throttle`), `Auth`, Quota in FileStore, `api.php`, `amicloud-admin.php`.
2. Web: Controller + Views (Dateien, Zuletzt, Suche, Freigaben, Einstellungen,
   Benutzer), `share.php`, CSS/JS.
3. Core `cmd_status_ex` → CLI, Shell, Handler, Commander Qt/MUI, Prefs-Text.
4. Doku.
5. Tests (s. §10), Deploy, Commits (AmiCloudCore, dann AmiCloud), Push.

## 10. Tests

- **Lokal:** MariaDB (Homebrew) + `php -S`, frische DB aus `schema.sql`;
  zusätzlich `migrate_001…004` gegen eine alte Schema-Kopie (Migrationspfad).
- `scripts/test_api.sh` unverändert grün (Rückwärtskompatibilität), erweitert
  um: App-Passwort-Login, `status.quota`, 507 bei Quota, 429 nach 5
  Fehlversuchen, deaktivierter Benutzer → 401.
- Web per Headless-Chrome-Screenshots + `curl`-Abläufe: Login, Upload
  (inkl. Quota-Fehler), Umbenennen/Verschieben/Löschen, Freigabe anlegen und
  als Gast (Passwort, Ablauf, ZIP) abrufen, Benutzer anlegen/bearbeiten/
  löschen, Schutz letzter Admin, CSRF-Ablehnung, Nicht-Admin bekommt 403 auf
  der Benutzerseite.
- `php -l` aller Dateien; PHP-7.4-Kompatibilität (Syntax) prüfen.
- Clients: `make pc`, `make qt`, `make amiga` (Cross-Compile), AmiOwnCloud
  und AmiVCS PC-Build gegen neuen Core; CLI `quota`/`version` gegen lokalen
  Server.
- **Produktion nach Deploy:** `migrate_004` als MariaDB-root, Admin per
  `amicloud-admin.php` anlegen bzw. Testbenutzer befördern (Nutzer gibt den
  Namen vor), `test_api.sh` gegen amicloud.planet-rk.de, Web-Rundgang.

## 11. Risiken / Hinweise

- **Produktions-`config.php` setzt `AMICLOUD_ALLOW_HTTP = true`** (lokale
  Test-Config). Apache leitet HTTP zwar auf HTTPS um, trotzdem beim Deploy
  entfernen.
- Der DB-Benutzer `amicloud` hat kein `ALTER`-Recht → Migrationen als
  MariaDB-root (`mysql amicloud < …` auf dem Server).
- Throttle per IP: Mehrere Amiga-Geräte hinter einem NAT teilen sich die
  Grenze; 5 Fehlversuche/15 min ist großzügig genug.
- Quota per DB-Summe: Dateien, die jemand direkt auf der Platte ablegt,
  zählen nicht – akzeptiert (Server-Speicher ist nur über API/Web befüllt).
- Ordner-ZIP großer Bäume: Zeitlimit; ZIP wird in eine Temp-Datei
  geschrieben und gestreamt, Grenze 1 GB Gesamtgröße mit Meldung.

## 12. Review-Auflagen (Runde 1) und Entscheidungen

Nummern entsprechen den Mängeln der Review.

1. **`share.php`**
   - Auslieferung **immer** als `Content-Disposition: attachment;
     filename*=UTF-8''…`, dazu `X-Content-Type-Options: nosniff`,
     `Content-Security-Policy: sandbox`, `Referrer-Policy: no-referrer`,
     `X-Robots-Tag: noindex, nofollow`. Kein Inline-Rendern hochgeladener
     Inhalte unter der eigenen Origin – auch nicht in `web.php` (Download dort
     ebenfalls `attachment` + `nosniff`).
   - Eigentümer kommt aus der DB (`users`-Zeile über `shares.user_id`);
     `Auth::$user_id/$username` werden daraus gesetzt. Eigentümer
     deaktiviert → Link liefert 404 (wie abgelaufen).
   - Unterpfad: `sanitizePath(sub)`, dann `share.path . sub`; bei
     Datei-Freigaben wird `sub` ignoriert. Typ (Datei/Ordner) muss zum
     aktuellen Dateisystem-Objekt passen, sonst 404.
   - Link-Passwort: Throttle pro (IP, Token), 10 Fehlversuche/15 min; nach
     Erfolg merkt sich die Session die Freischaltung des Tokens.
2. **Gemeinsamer Schreibweg** `FileStore::uploadImpl(path, srcFile|data,
   md5) → [code, msg]` für API und Web: Quota-Prüfung, `revision`,
   `bumpGlobalRevision`, `change_seq`, `deleted_at = NULL`, MIME, Checksumme.
   `upload()` und `uploadRaw()` werden dünne Hüllen. Quota-Prüfung und
   Schreiben in einer Transaktion mit `SELECT … FOR UPDATE` auf die
   `users`-Zeile.
3. **Benutzername aus der DB.** `Auth::$username` wird überall (API, Web,
   share.php) aus dem DB-Datensatz gesetzt, nie aus der Eingabe. Neue Namen:
   nur Kleinbuchstaben, `^[a-z0-9][a-z0-9._-]{0,63}$`; Eingaben beim Login
   werden nicht umgeschrieben (die Collation findet „BOB“ – der Speicherpfad
   kommt aber aus der Zeile).
4. **Löschschutz.** `deleteImpl('/')` → 400 „Cannot delete root“ (API und
   Web/Bulk). `Users::delete`: Name gegen Regex, Ziel =
   `STORAGE_PATH/<name>`, `realpath(dirname(ziel)) === realpath(STORAGE_PATH)`,
   erst dann `deleteTree`. Anlegen (Web und CLI) wird abgelehnt, wenn
   `STORAGE_PATH/<name>` existiert und nicht leer ist.
5. **Auth/Throttle/Timing.**
   - Format-Erkennung zuerst: passt das Passwort auf
     `^[a-z2-9]{5}(-[a-z2-9]{5}){4}$`, nur SHA-256-Lookup (kein bcrypt);
     sonst bcrypt gegen `pass_hash`. Benutzer unbekannt → Dummy-
     `password_verify` (gleiche Laufzeit).
   - `last_used_at` nur schreiben, wenn älter als 5 min.
   - Throttle: Grenze 5 pro (IP, Benutzer) und 30 pro IP in 15 min → 429
     (API) bzw. Hinweis „Zu viele Fehlversuche, bitte später erneut“ (Web).
     Zeilen werden nur im Fehlerfall geschrieben; dabei werden Zeilen älter als
     1 Tag gelöscht. Erfolgreiche Anmeldung löscht die Zeilen dieses
     (IP, Benutzer). Kein DB-Schreibzugriff im Erfolgsfall außer
     `last_used_at`/`last_login` (letzteres nur Web).
   - Alle Text-Eingaben werden vor dem Schreiben auf die Spaltenlänge geprüft
     (Web: Fehlermeldung; Throttle: `substr`).
   - Web-Login akzeptiert **nur** das Kontopasswort, keine App-Passwörter.
6. **Freigaben und Umbenennen.** `Shares::onRename(from, to)` und
   `onDelete(path)` behandeln exakten Pfad **und** Präfix `pfad/%`
   (`likeEscape`). `renameImpl` löscht vor dem `UPDATE` Tombstones am Ziel
   und unter dem Zielpräfix; DB-Teil in einer Transaktion. Gilt für API und
   Web („Verschieben“ nutzt `renameImpl`).
7. **Session-Invalidierung.** Neue Spalte `users.session_epoch INT UNSIGNED
   NOT NULL DEFAULT 0`; erhöht bei Passwortwechsel, Admin-Reset und
   Deaktivierung. Die Session speichert den Wert; `loadSessionUser()`
   vergleicht ihn bei jedem Request. Beim eigenen Passwortwechsel
   `session_regenerate_id(true)` und neuer Wert in der Session (man bleibt
   angemeldet). App-Passwörter überleben einen Passwortwechsel (wie
   ownCloud); Deaktivieren sperrt alles.
8. **Quota-Darstellung.**
   - API `status.quota`: `{"used": n, "total": n, "free": n, "unlimited":
     bool}`. Unbegrenzt → `free = disk_free_space(STORAGE_PATH)`,
     `total = used + free` (wie ownCloud). `free` nie negativ.
   - `AcStatus` liegt in `cloudcore/entry.h` (geteilte Typen gehören in den
     Core), Funktion `cmd_status_ex()` in `cloudhttp/cmd.h/.c`:
     ```c
     typedef struct {
         char version[32];
         long global_revision;
         int  has_quota;        /* 0 = alter Server ohne quota-Feld */
         int  quota_unlimited;
         long quota_used_kb;    /* KiB, gerundet nach oben */
         long quota_total_kb;
         char user[64];
         char display_name[128];
     } AcStatus;
     ```
     Umrechnung über `double`, Werte > `LONG_MAX` KiB werden geklemmt.
   - Größenformat einmal in `libcloudcore`: `ac_format_kb(long kb, char
     *buf, size_t n)` → „1,2 MB“, „5,0 GB“ (additiv). Nutzer: CLI, Shell,
     MUI, Qt.
9. **Handler blockiert nie wegen Info.** Der Backend-Timer ist deaktiviert
   (`AC_BACKEND_TIMER_ENABLED 0`), ein eigener Netzabruf aus `ACTION_INFO`
   könnte Workbench unbegrenzt einfrieren. Daher **kein** neuer
   Worker-Request: Der Worker hängt die Quota an vorhandene Antworten an.
   - `WorkerMsg` bekommt `LONG quota_used_kb, quota_total_kb; BYTE
     quota_valid;` (out).
   - Der Worker ruft `cmd_status_ex()` im Anschluss an ein erfolgreiches
     `WREQ_LIST`, wenn sein letzter Abruf > 30 s her ist oder seit dem letzten
     Abruf ein `WREQ_PUT`/`WREQ_DEL` erfolgreich war, und füllt die Felder.
   - Der Handler übernimmt gültige Werte in einen Cache;
     `ACTION_INFO`/`ACTION_DISK_INFO` antworten **immer sofort** aus dem
     Cache, vor dem ersten Wert mit dem bisherigen Platzhalter.
   - `BytesPerBlock = 1024`; `NumBlocks = total_kb`, `NumBlocksUsed =
     min(used_kb, total_kb)`.
   - `map_get_status` **und** `map_put_status`: 429 → `ERROR_DISK_NOT_VALIDATED`;
     `map_put_status`: 507 → `ERROR_DISK_FULL`.
10. **ADR 05** „Mehrbenutzerbetrieb, Quotas und öffentliche Links“ schreibt
    ADR 04 fort (Freigaben = nur öffentliche Links; Gruppen/ACLs/gemeinsame
    Ordner weiterhin nicht).
11. **CLI `version`** bleibt offline und unverändert; Belegung nur über
    `amicloud quota`.
12. **Raw-Varianten fürs Web:** `FileStore::searchRaw()` (Array statt JSON);
    `search()` wird dünne Hülle. Zielordner für „Verschieben“: kleiner
    JSON-Endpunkt `web.php?ajax=dirs&path=…` liefert die Unterordner aus dem
    **Dateisystem**, der Dialog klappt Ordner lazy auf.
13. **PHP 7.4:** Kein `str_contains/str_starts_with/str_ends_with`, kein
    `?->`, keine Named Arguments, keine Constructor Promotion, kein `mixed`,
    kein `static`-Rückgabetyp, kein `catch` ohne Variable, kein `match`, keine
    Union-Types/Enums. Prüfung mit echtem PHP 7.4 (`php -l` auf dem Server,
    Dateien in einem Temp-Verzeichnis außerhalb des Web-Roots).

### Weitere Anmerkungen der Review – übernommen

- Admin-CLI liegt als `server/bin/amicloud-admin.php` (Ordner per
  `.htaccess` gesperrt), Aufruf `sudo -u apache php bin/amicloud-admin.php …`
  (INSTALL.md nennt `apache`/`www-data` je nach System); Passwort-Eingabe
  ohne Echo (`stty -echo`) oder `--password-stdin`.
- Logout per POST mit CSRF (Benutzermenü).
- `index.php`: `session_start()` nur, wenn ein Session-Cookie existiert;
  „Zu meinen Dateien“ ohne DB/`config.php`.
- Commander Qt: Belegung in der **globalen Statusleiste** (keine
  Panel-Statuszeile vorhanden), Abruf im vorhandenen Hintergrund-/Job-
  Mechanismus, nicht synchron im UI-Thread. MUI: Panel-Statuszeile des
  Cloud-Panels.
- `expires_at` wird in PHP gegen `date('Y-m-d')` verglichen.
- ZIP: Temp-Datei, Aufräumen per `register_shutdown_function`.
- Neue Tabellen mit `ENGINE=InnoDB DEFAULT CHARSET=utf8mb4`.
- Doku-Hinweis: alte Amiga-Programme rechnen freie Bytes evtl. 32-bit
  (> 4 GB frei kann falsch angezeigt werden).
- **Namenskonflikt beim Web-Upload:** Der Browser kennt die Namen im
  aktuellen Ordner; bei Treffern Dialog „N Datei(en) existieren bereits:
  Ersetzen / Überspringen / Abbrechen“ (Überspringen filtert clientseitig).
- **Reihenfolge der Umsetzung:** (1) Schema, Benutzer, Auth, Throttle, Quota,
  Admin-CLI, Bestandsfehler 2/3/4/6 → (2) Web-Redesign inkl. Einstellungen und
  Benutzerverwaltung → (3) öffentliche Links → (4) Core + Clients →
  (5) Doku → Tests, Deploy, Commit, Push.

### Umsetzungshinweise aus Review Runde 2

- Worker: `cmd_status_ex()` erst nach erfolgreichem LIST (`m->result = 0`);
  ein Fehlschlag ändert `m->result` nicht, nur `quota_valid = 0`.
- Handler: Quota-Cache in `vol.c` (statisch, Getter), übernommen in
  `vol_refresh()` analog zu `vol_note_now()`; `fill_info()` liest nur.
- Kontopasswörter, die dem App-Passwort-Muster entsprechen, werden beim
  Setzen abgelehnt.
