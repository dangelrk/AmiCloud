# AmiCloud REST-API

Basis-URL: `https://amicloud.example.com/api.php`

Alle Anfragen laufen ausschließlich über **HTTPS** (Plain-HTTP → 403).
Authentifizierung per **HTTP Basic Auth** bei jedem Request — stateless,
kein Cookie, kein Token. Als Passwort gilt das **Kontopasswort** oder ein
**App-Passwort** desselben Benutzers (in der Weboberfläche unter
*Persönliche Einstellungen → Geräte & App-Passwörter* angelegt, Format
`xxxxx-xxxxx-xxxxx-xxxxx-xxxxx`, einzeln widerrufbar; empfohlen für alle
Clients). Kontopasswörter liegen als bcrypt-Hash in MariaDB,
App-Passwörter als SHA-256. Deaktivierte Benutzer erhalten 401. Nach
5 Fehlversuchen pro Benutzer und IP (bzw. 30 pro IP) innerhalb von
15 Minuten antwortet der Server mit **429** `Too many failed logins`
(`Retry-After: 900`), auch bei danach richtigem Passwort.

Downloads (`action=download`) kommen immer als
`application/octet-stream` mit `Content-Disposition: attachment` und
`Content-Security-Policy: sandbox`. Ändernde Aktionen (`upload`, `delete`,
`mkdir`, `rename`, `setcomment`, `setprotect`, `setdate`) lehnt der Server
mit 403 ab, wenn der Browser sie von einer fremden Seite auslöst
(`Sec-Fetch-Site` gesetzt und nicht `none`); die Clients senden diesen
Header nie. Pfade dürfen nur gültiges UTF-8 enthalten, jedes Segment
höchstens 255 Byte, der ganze Pfad höchstens 1000 Byte (sonst 400), und
werden exakt verglichen (Groß/Klein zählt).

Benutzer, Kontingente und öffentliche Links werden in der Weboberfläche
bzw. mit `bin/amicloud-admin.php` verwaltet, nicht über diese API.

Anfragen sind einfache Query-Strings; HTTP/1.0 mit `Connection: close`
genügt (kein Chunked Transfer, kein Keep-Alive nötig). Der Amiga-Client
(`core/libcloudhttp/src/http_amiga.c`) hält die Verbindung per
HTTP/1.1-Keep-Alive offen, wenn der Server das anbietet, und fällt sonst
auf eine Verbindung pro Anfrage zurück.

## Antwortformat

Erfolg (`HTTP 200`):

```json
{ "status": "ok", ... }
```

Fehler (HTTP-Code = `code`):

```json
{ "status": "error", "code": 404, "message": "File not found" }
```

`download` liefert rohe Dateibytes statt JSON.

## Pfade

`path` ist immer ein Pfad relativ zum Benutzer-Wurzelverzeichnis,
beginnend mit `/`. `..` und die Zeichen `\x00 < > : " | ? *` sind
verboten (→ 400). Jeder Benutzer sieht nur seinen eigenen Baum
(`/var/amicloud-storage/{username}/`).

## Zeitstempel

Alle String-Zeitstempel (`status.time`, `list`/`info`: `created`/`modified`,
`setdate`) verwenden das Format `YYYY-MM-DD HH:MM:SS`, ohne Zeitzonen-
Suffix. Es ist **bewusst keine UTC**, sondern die lokale Zeitzone des
Servers (`AMICLOUD_TIMEZONE` in `config.php`, Standard `Europe/Berlin`) —
dieselbe Zeitzone, die auch der Amiga-Client für seine Systemuhr
verwendet, damit Server- und Amiga-Zeitstempel ohne Offset-Umrechnung
direkt vergleichbar sind. Lokale Auflistungen (`lls`/`ldir`/`llist` im
CLI-Client) verwenden dasselbe Format.

Zusätzlich liefern `list`/`info`/`upload`/`setdate` ein maschinenlesbares
`mtime`-Feld (UTC-Epoche-Sekunden, `filemtime()`) — für die *Anzeige*
bleibt das lokale String-Format maßgeblich, `mtime` dient ausschließlich
dem rechnerübergreifenden Vergleich (`sync`, Abschnitt „Client" unten).
Auf dem Amiga hat die Systemuhr keinerlei Zeitzoneninfo; der Client
rechnet über `gmt_offset`/`dst` aus der Config zwischen `DateStamp` und
UTC-Epoche um (nur dort relevant, PC/Mac/Linux-Uhren liefern bereits
UTC-Epoche).

---

## Actions

### status — Server-Version / Health-Check

```
GET api.php?action=status
```

```json
{ "status": "ok", "version": "0.1", "time": "2026-07-04 12:00:00", "global_revision": 42,
  "user":  { "name": "bob", "display_name": "Bob Beispiel" },
  "quota": { "used": 1234567, "total": 5368709120, "free": 5367474553, "unlimited": false },
  "max_upload": 104857600 }
```

Erfordert wie alle Actions Basic Auth; dient als leichter Verbindungs-Check (z.&nbsp;B. Start-Banner
eines interaktiven Clients) und liefert die Server-Version. `global_revision` ist der aktuelle Stand
des in `changes` beschriebenen Änderungszählers dieses Users — ein Client kann damit billig prüfen,
ob sich seit dem letzten Sync überhaupt etwas geändert hat, ohne `list`/`changes` aufzurufen.

`user` und `quota` liefern Server ab Migration 004 (Benutzerverwaltung); ältere Clients ignorieren
die Felder. `quota` in Bytes: `used` = Summe aller Dateien des Benutzers, `free` nie negativ. Bei
unbegrenztem Kontingent (`unlimited: true`) ist `free` der freie Plattenplatz des Servers und
`total = used + free` – wie bei ownCloud, damit Clients immer eine sinnvolle Gesamtgröße anzeigen
können (der `AMICLOUD:`-Handler meldet daraus die Laufwerksbelegung).
`max_upload` (seit 0.2) ist die größte Datei in Bytes, die `upload` annimmt.

### list — Verzeichnis auflisten

```
GET api.php?action=list&path=/sub
```

```json
{
  "status": "ok",
  "path": "/sub",
  "entries": [
    { "name": "file.txt", "type": "file", "size": 4096,
      "modified": "2026-06-25 14:30:00", "mtime": 1719325800,
      "md5": "d41d8cd98f00b204e9800998ecf8427e",
      "protection": 0, "comment": "",
      "id": 17, "revision": 3, "change_seq": 41 },
    { "name": "bilder", "type": "dir", "size": 0,
      "modified": "2026-06-20 09:00:00", "mtime": 1718870400,
      "md5": "",
      "protection": 0, "comment": "",
      "id": 12, "revision": 1, "change_seq": 12 }
  ]
}
```

`protection` sind rohe Amiga-Protection-Bits (RWED-Konvention: gesetztes
Bit = Recht entzogen; Bit 0=DELETE, 1=EXECUTE, 2=WRITE, 3=READ,
4=ARCHIVE, 5=PURE, 6=SCRIPT, 7=HIDDEN). `comment` ist der
Amiga-Filecomment (max. 79 Zeichen). `mtime` ist dieselbe Änderungszeit
wie `modified`, aber als UTC-Epoche-Sekunden — maschinenlesbar für
Sync-Vergleiche über Rechnergrenzen hinweg (siehe Abschnitt „Zeitstempel"
unten). `md5` ist die 32-stellige Hex-Prüfsumme der Datei (leer bei
Verzeichnissen); bei Bedarf berechnet der Server sie einmalig und
speichert sie (siehe `upload`). `id` ist die interne Datei-ID, `revision`
die Inhalts-Revision (1 bei Neuanlage, `++` nur bei Inhaltsänderung,
Rename/Move lässt sie unverändert) und `change_seq` der Stand von
`global_revision` beim letzten Change dieser Zeile — alle drei sind 0,
wenn der Pfad (noch) keine eigene Metadaten-Zeile hat (z.&nbsp;B. ein nur
implizit als Upload-Parent entstandenes Verzeichnis); ein Client, der sie
nicht kennt oder eine 0 vorfindet, fällt auf reinen MD5-Vergleich zurück.

Fehler: 404 (Verzeichnis existiert nicht).

### changes — Delta seit einer Revision abfragen

```
GET api.php?action=changes&since=N
GET api.php?action=changes&since=N&path=/Fotos&limit=500
```

```json
{
  "status": "ok",
  "global_revision": 42,
  "has_more": false,
  "next": 42,
  "entries": [
    { "path": "/sub/file.txt", "type": "file", "size": 4096,
      "md5": "d41d8cd98f00b204e9800998ecf8427e", "mtime": 1719325800,
      "revision": 3, "change_seq": 41, "id": 17, "deleted": false },
    { "path": "/sub/geloescht.txt", "type": "file", "size": 0,
      "md5": "", "mtime": 0,
      "revision": 2, "change_seq": 42, "id": 9, "deleted": true }
  ]
}
```

Liefert die Zeilen dieses Users mit `change_seq` &gt; `since`, aufsteigend nach `change_seq`. Sie
sind nicht auf ein Verzeichnis beschränkt wie bei `list`, deshalb trägt jeder Eintrag seinen
vollständigen Pfad mit führendem `/` in `path` statt nur eines `name`.

- **Gelöschte Pfade** erscheinen mit `"deleted": true`. Das ist die einzige Stelle der API, an der
  ein Client eine Löschung von einer Neuanlage auf der Gegenseite unterscheiden kann.
- **Umbenennen und Verschieben** liefern den neuen Pfad (und alles darunter) als lebende Einträge
  und den alten Pfad (und alles darunter) als gelöschte, alle mit derselben `change_seq`.
  Schon gelöschte Pfade darunter bleiben dabei, wo sie waren.
- **Ordner** erscheinen auch dann, wenn sie nur durch einen Upload darunter entstanden sind: Upload,
  `mkdir` und `rename` legen fehlende Elternordner-Zeilen an und beleben gelöschte wieder. Ein
  gelöschter Ordner bekommt immer einen eigenen Grabstein.
- **`path`** (optional) schränkt auf diesen Ordner und alles darunter ein (`/` = alles).
- **Seiten:** höchstens `limit` Zeilen (Vorgabe 1000, höchstens 5000). Gibt es mehr, ist
  `has_more` wahr; die nächste Seite holt man mit `since` = `next`. Eine `change_seq` wird nie auf
  zwei Seiten verteilt. Ein umbenannter Ordner kommt also ganz, auch wenn er größer als `limit` ist.
- **`mtime`** ist wie bei `list` die Zeit der Datei im Dateisystem (Epochensekunden). `md5` ist wie
  bei `list` ausgefüllt. Bei gelöschten Einträgen sind `mtime`, `size` und `md5` leer bzw. 0.
- **`next`** ist ohne weitere Seiten mindestens `global_revision`.
- **Grabsteine** (gelöschte Zeilen) werden nach 30 Tagen entfernt. Fragt ein Client mit einem
  `since` vor dem ältesten entfernten Grabstein, antwortet der Server mit **410** (`since too old`).
  Der Client muss dann alles mit `list` neu einlesen.
- `since=0` liefert den ganzen Bestand.

Ein Client, der `since` gleich seiner zuletzt gesehenen `global_revision` setzt und eine leere
`entries`-Liste zurückbekommt, weiß, dass sich seit dem letzten Abgleich nichts geändert hat, ohne
einen ganzen Verzeichnisbaum abzufragen. Der Abgleich der Clients (ab 0.3) nutzt `changes`, sobald
sich `global_revision` bewegt hat, und liest bei jedem 20. Lauf alles mit `list`. Enthält die
Änderungsliste Löschungen, prüfen sie zusätzlich mit einem `list`, ob ihr Startordner noch existiert.

Benötigt Migration `migrate_006.sql`.

### search — Datei-/Ordnernamen suchen

```
GET api.php?action=search&path=/sub&q=*.txt
GET api.php?action=search&path=/&q=#?.info&dirs=0
```

```json
{
  "status": "ok",
  "entries": [
    { "path": "/sub/file.txt", "type": "file", "size": 4096,
      "md5": "d41d8cd98f00b204e9800998ecf8427e", "mtime": 1719325800,
      "revision": 3, "change_seq": 41, "id": 17 }
  ],
  "truncated": false
}
```

Sucht serverseitig (ein einziger DB-Request, kein Verzeichnis-für-Verzeichnis-Walk
vom Client aus) nach allen lebenden Zeilen dieses Users unterhalb von `path`
(rekursiv, `path=/` = ganzer Baum), deren **Basename** (letztes Pfadsegment) auf
das Joker-Muster `q` passt. Unterstützt dieselbe plattformübergreifende
Joker-Syntax wie die Client-seitigen `get`/`put`-Muster: `*` und `#?` (beliebig
viele Zeichen, austauschbar) sowie `?` (genau ein Zeichen). Reine
AmigaDOS-Musterklassen (`[abc]`, `(a|b)`) werden nicht übersetzt. `dirs`
(Default `1`/true) schaltet Verzeichnisse in den Treffern ab, wenn auf `0`
gesetzt. Jeder Treffer hat dieselbe Feldform wie ein `changes`-Eintrag (ohne
`deleted`, das hier nicht vorkommen kann), mit vollständigem, führend-`/`-Pfad.
Die Ergebnisliste ist auf 2000 Treffer gedeckelt; `truncated: true` zeigt an,
dass es weitere gäbe.
Fehler: 400 (`q` fehlt/leer).

### download — Datei herunterladen

```
GET api.php?action=download&path=/sub/f
GET api.php?action=download&path=/sub/f&offset=N&length=M
```

Antwort: rohe Dateibytes. Mit `offset`/`length` nur der Teilbereich
(M Bytes ab Byte N) — genutzt vom Filesystem-Handler für Seek. Bei einem
vollständigen Download (kein `offset`/`length`) liefert der Server
zusätzlich den Header `X-AmiCloud-MD5: <32 Hex-Zeichen>` — bei
Teil-Downloads bewusst weggelassen, da die Prüfsumme immer für die
Gesamtdatei gilt.

Ohne `offset`/`length` versteht der Server auch einen einzelnen
`Range: bytes=N-` bzw. `Range: bytes=N-M` und antwortet mit
`206 Partial Content` und `Content-Range`. Damit setzen die Clients
einen abgebrochenen Download an der Abbruchstelle fort (die Prüfsumme
aus dem ersten, vollständigen Abruf gilt weiter). Ein Bereich hinter dem
Dateiende ergibt 416; andere Range-Formen werden ignoriert (volle
Antwort, 200).
Fehler: 404, 416.

### upload — Datei hochladen

```
POST api.php?action=upload&path=/sub/f
POST api.php?action=upload&path=/sub/f&md5=<32 Hex-Zeichen>
POST api.php?action=upload&path=/sub/f&if_revision=3&if_id=815
POST api.php?action=upload&path=/sub/f&create_only=1
Body: rohe Dateibytes (Content-Type: application/octet-stream)
```

Leerer Body ist erlaubt und erzeugt eine 0-Byte-Datei. Existierende
Dateien werden überschrieben; fehlende Elternverzeichnisse werden
angelegt. Der optionale Parameter `md5` lässt den Server die Prüfsumme
des empfangenen Bodys **vor** dem Schreiben verifizieren — stimmt sie
nicht überein, wird nichts geschrieben (422). Ohne den Parameter
unverändertes Verhalten (abwärtskompatibel für Handler/ältere Clients).

Bedingungen für den Abgleich (seit 0.2): Mit `if_revision=N` bzw.
`if_id=M` wird nur geschrieben, wenn die Datei existiert und genau diese
Revision bzw. ID hat (eine auf den Pfad umbenannte andere Datei behält ihre
Revision, hat aber eine andere ID; Werte ≤ 0 bedeuten „nicht prüfen“); mit
`create_only=1` nur, wenn sie noch nicht existiert. Sonst antwortet der
Server mit **412** (`Revision mismatch` bzw. `File exists`) und schreibt
nichts. Geprüft wird in derselben Transaktion wie das Schreiben, also
atomar gegenüber anderen Uploads desselben Benutzers. So überschreibt ein
Client keine Änderung, die nach seinem letzten `list` dazukam.

```json
{ "status": "ok", "path": "/sub/f", "size": 4096, "md5": "d41d8cd98f00b204e9800998ecf8427e",
  "mtime": 1719325800, "revision": 3, "change_seq": 41, "id": 815 }
```

`revision` und `change_seq` wie bei `list`/`info` (s.&nbsp;o.). Ein Re-Upload eines zuvor gelöschten
Pfades belebt dessen Zeile wieder (inkl. Übernahme der bisherigen `revision`, sofern der Inhalt mit
dem Stand vor der Löschung übereinstimmt).

Fehler: 400 (`path=/`), 409 (an dem Pfad liegt ein Verzeichnis), 412
(Bedingung `if_revision`/`create_only` nicht erfüllt), 413
(größer als `MAX_UPLOAD`), 422 (`md5`-Parameter stimmt nicht mit dem
empfangenen Body überein), **507** `Quota exceeded` (die Datei würde das
Speicherkontingent überschreiten – es wird nichts geschrieben; beim
Überschreiben zählt nur die Größendifferenz), 500.

### delete — Datei/Verzeichnis löschen

```
DELETE api.php?action=delete&path=/sub/f
DELETE api.php?action=delete&path=/sub&recursive=1
DELETE api.php?action=delete&path=/sub/f&if_revision=3&if_id=815
```

Ohne `recursive` müssen Verzeichnisse leer sein (sonst 409). Mit `recursive=1` wird der komplette
Baum von der Platte gelöscht (Dateien und Unterverzeichnisse) — irreversibel, es gibt keine
Bestätigung auf API-Ebene, das übernimmt der Client. Die zugehörigen Metadaten-Zeilen (Kommentar/
Protection/Revision) bleiben dagegen als **Tombstone** in der DB erhalten (`deleted_at` gesetzt,
`change_seq` erhöht) statt gelöscht zu werden — sichtbar über `list`/`info` verschwindet der Pfad
sofort (die Datei existiert ja nicht mehr), aber `changes` liefert ihn mit `"deleted": true`, damit
ein Client die Löschung von einer echten Neuanlage auf der Gegenseite unterscheiden kann.
Öffentliche Links auf den Pfad (und alles darunter) werden mitgelöscht.

Bedingtes Löschen für den Abgleich (seit 0.2): Mit `if_revision` und/oder `if_id` wird nur eine
Datei gelöscht, die noch genau diese Revision bzw. ID hat; sonst **412** und nichts wird gelöscht.
So nimmt ein Client keine fremde Änderung mit, die nach seinem letzten `list` dazukam. Geprüft unter
derselben Sperre wie `upload`.
Fehler: 400 (`path=/` – der Stammordner selbst ist nicht löschbar), 404, 409 (nicht-leer, nur ohne
`recursive`), 412 (Bedingung nicht erfüllt), 500.

### info — Datei-/Verzeichnisinformationen

```
GET api.php?action=info&path=/sub/f
```

```json
{
  "status": "ok",
  "name": "f", "type": "file", "size": 4096,
  "mime": "text/plain",
  "created":  "2026-06-01 10:00:00",
  "modified": "2026-06-25 14:30:00", "mtime": 1719325800,
  "md5": "d41d8cd98f00b204e9800998ecf8427e",
  "protection": 13, "comment": "Wichtiger Bericht",
  "id": 17, "revision": 3, "change_seq": 41
}
```

Bei Verzeichnissen (`type: "dir"`) ist `size` die **rekursive Gesamtgröße** aller enthaltenen
Dateien inklusive aller Unterverzeichnisse (nicht 0), `md5` ist leer. `list` liefert für
Verzeichniseinträge weiterhin `size: 0` — die rekursive Berechnung läuft nur für ein einzelnes,
explizit angefragtes Verzeichnis in `info`. `mtime`/`md5` wie bei `list` (s.o.).
Fehler: 404.

### mkdir — Verzeichnis anlegen

```
POST api.php?action=mkdir&path=/neu
```

Fehler: 409 (existiert bereits), 500.

### rename — Umbenennen/Verschieben

```
POST api.php?action=rename&path=/alt&to=/neu
```

Funktioniert für Dateien **und Verzeichnisse** (Metadaten aller
enthaltenen Einträge ziehen mit um). Quelle und Ziel dürfen in
verschiedenen Verzeichnissen liegen (= Verschieben). Das
Elternverzeichnis des Ziels muss existieren. Öffentliche Links ziehen mit um.

Fehler: 400 (Root, oder ein Verzeichnis soll in sich selbst verschoben werden),
404 (Quelle/Ziel-Parent fehlt), 409 (Ziel existiert), 500.

### setcomment — Amiga-Filecomment setzen

```
POST api.php?action=setcomment&path=/sub/f&comment=Text
```

`comment` URL-encodiert, max. 79 Zeichen (länger wird gekürzt,
Steuerzeichen werden entfernt). Leerer Wert löscht den Kommentar.
Fehler: 404.

### setprotect — Amiga-Protection-Bits setzen

```
POST api.php?action=setprotect&path=/sub/f&bits=13
```

`bits` = rohe Amiga-Maske (unsigned, s.o.). Der Server wertet die Bits
nicht aus — sie werden gespeichert und über `list`/`info` zurückgegeben;
die Durchsetzung übernimmt der Amiga-Client/Handler.
Fehler: 400 (negativ), 404.

### setdate — Änderungsdatum setzen

```
POST api.php?action=setdate&path=/sub/f&modified=2020-05-01%2012:30:00
POST api.php?action=setdate&path=/sub/f&mtime=1719325800
```

Zwei gleichwertige Formen: `modified` strikt `YYYY-MM-DD HH:MM:SS`
(Leerzeichen im Query-String als `%20` kodieren) in der lokalen
Server-Zeitzone (`AMICLOUD_TIMEZONE` in `config.php`, Standard
`Europe/Berlin`) — dieselbe Zeitzone, in der auch `list`/`info` ihre
Zeitstempel ausgeben. Alternativ `mtime` als UTC-Epoche-Sekunden,
zeitzonenfrei — das nutzt `sync` (Client), um die Remote-Mtime exakt auf
die Quelldatei zu setzen. Ist `mtime` gesetzt, hat es Vorrang vor
`modified`. Setzt DB-Metadatum **und** Datei-mtime (damit `list`/`info`
konsistent bleiben).

```json
{ "status": "ok", "path": "/sub/f", "modified": "2020-05-01 12:30:00", "mtime": 1588329000 }
```

Fehler: 400 (Format), 404.

---

## Fehlercodes (Übersicht)

| Code | Bedeutung |
|------|-----------|
| 400  | Ungültige Action, Pfad-Traversal, ungültige Parameter |
| 401  | Basic Auth fehlt/falsch, Benutzer deaktiviert |
| 403  | Plain-HTTP statt HTTPS |
| 404  | Objekt nicht gefunden |
| 409  | Objekt existiert bereits / Verzeichnis nicht leer |
| 413  | Upload zu groß |
| 422  | `upload`: `md5`-Parameter stimmt nicht mit dem empfangenen Body überein |
| 429  | Zu viele fehlgeschlagene Anmeldungen (15 Minuten warten) |
| 507  | `upload`: Speicherkontingent überschritten |
| 500  | Serverfehler (Filesystem/DB) |

## Testsuite

`scripts/test_changes.py` prüft `changes` (Grabsteine, Seiten, Präfix, Zeitbasis).
`scripts/test_api.sh` prüft 11 der 13 Actions (alle außer `changes` und
`search`) gegen einen laufenden Server, inklusive der MD5-/Mtime-Ergänzungen:

```sh
AC_SERVER=https://amicloud.example.com AC_USER=bob AC_PASS=geheim \
    sh scripts/test_api.sh
```
