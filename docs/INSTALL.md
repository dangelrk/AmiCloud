# AmiCloud installieren

AmiCloud besteht aus einem eigenen **Server** (PHP + MariaDB) und
**Client-Programmen** für den Amiga und den PC. Zuerst wird der Server
eingerichtet, danach die Clients.

- [Server](#server)
- [Amiga](#amiga)
  - [Voraussetzungen](#voraussetzungen-amiga)
  - [Installation mit dem Install-Skript](#installation-mit-dem-install-skript)
  - [Manuelle Installation](#manuelle-installation)
  - [Konfiguration](#konfiguration-amiga)
  - [CLOUD: mounten](#cloud-mounten)
  - [Deinstallation](#deinstallation-amiga)
- [PC: macOS, Linux, Windows](#pc)

Wie die Programme gebaut werden, steht in [BUILD.md](BUILD.md), wie sie
bedient werden, im Handbuch ([AmiCloud-Handbuch.html](user/AmiCloud-Handbuch.html)
bzw. [AmiCloud.guide](user/AmiCloud.guide)).

In allen Beispielen steht `amicloud.example.com` für den eigenen Server,
`bob` für den Benutzernamen und `geheim` für das Passwort.

---

## Server

Der Server liegt im Ordner `server/`:

| Datei | Aufgabe |
|---|---|
| `api.php` | REST-API für alle Client-Programme (Referenz: [api/API.md](api/API.md)) |
| `web.php`, `web.css` | Weboberfläche: Anmelden, Dateien ansehen, hoch-/herunterladen, umbenennen, löschen, Ordner anlegen |
| `includes/` | PHP-Klassen (per `.htaccess` für Browser gesperrt) |
| `sql/schema.sql` | Datenbankschema für eine Neuinstallation |
| `sql/migrate_001.sql` … `migrate_003.sql` | Nachrüsten älterer Installationen |
| `config.php.example` | Vorlage für `config.php` |
| `.htaccess` | sperrt `config.php` und `config.php.example` |

### Voraussetzungen (Server)

- Linux-Server mit **Apache 2.4** (für die `.htaccess`-Dateien wird
  `AllowOverride All` benötigt)
- **PHP 7.4 oder neuer** (PHP 8 geht ebenso) mit den Erweiterungen
  `pdo_mysql` und `fileinfo`
- **MariaDB** oder MySQL
- ein DNS-Name und ein TLS-Zertifikat (z. B. Let's Encrypt): die API
  lehnt Klartext-HTTP ab (Antwort 403), die Weboberfläche setzt ihr
  Sitzungs-Cookie nur über HTTPS

Die folgenden Schritte sind für eine RHEL-artige Distribution (AlmaLinux,
Rocky, Fedora: Apache-Benutzer `apache`, Konfiguration unter
`/etc/httpd/`) geschrieben. Unter Debian/Ubuntu heißen Benutzer und Ordner
`www-data` bzw. `/etc/apache2/sites-available/`, der Ablauf ist derselbe.

### 1. Virtual Host anlegen

`web.php` verlinkt sich selbst mit absoluten Pfaden (`/web.php`), deshalb
gehört der Server **an die Wurzel eines eigenen (Sub-)Domainnamens**:

```apache
# /etc/httpd/conf.d/amicloud.conf
<VirtualHost *:80>
    ServerName   amicloud.example.com
    DocumentRoot /var/www/amicloud
    DirectoryIndex disabled
    <Directory /var/www/amicloud>
        Options None
        AllowOverride All
        Require all granted
    </Directory>
</VirtualHost>
```

### 2. HTTPS einrichten

```sh
sudo certbot --apache -d amicloud.example.com
```

certbot legt den HTTPS-Block an und leitet HTTP auf HTTPS um.

### 3. Dateien kopieren

```sh
sudo mkdir -p /var/www/amicloud /var/amicloud-storage
sudo cp -r server/. /var/www/amicloud/
sudo cp /var/www/amicloud/config.php.example /var/www/amicloud/config.php
sudo chown -R apache:apache /var/www/amicloud /var/amicloud-storage
sudo chmod 750 /var/amicloud-storage
```

Wichtig: `server/.` mit Punkt kopieren, nicht `server/*` – das Sternchen
lässt die `.htaccess`-Dateien weg, und dann wären `config.php` und
`includes/` im Browser abrufbar. Wer vom eigenen Rechner hochlädt:
`scp -r server/. benutzer@amicloud.example.com:/var/www/amicloud/`.

Das Speicherverzeichnis (`/var/amicloud-storage`) muss **außerhalb** des
Web-Verzeichnisses liegen. Jeder Benutzer bekommt darin automatisch einen
eigenen Unterordner (beim ersten Zugriff).

### 4. `config.php` ausfüllen

```php
define('AMICLOUD_TIMEZONE', 'Europe/Berlin');   // Zeitzone aller Zeitstempel
define('DB_HOST',       'localhost');
define('DB_NAME',       'amicloud');
define('DB_USER',       'amicloud');
define('DB_PASS',       'ein-langes-db-passwort');
define('STORAGE_PATH',  '/var/amicloud-storage');
define('MAX_UPLOAD',    100 * 1024 * 1024);      // Bytes
/* define('AMICLOUD_ALLOW_HTTP', true); */       // NUR lokale Entwicklung
```

| Konstante | Bedeutung |
|---|---|
| `AMICLOUD_TIMEZONE` | Zeitzone für alle Zeitangaben der API (bewusst nicht UTC). Sollte zur Uhr der Amiga-Clients passen, siehe `gmt_offset`/`dst` in der Client-Konfiguration |
| `DB_HOST`, `DB_NAME`, `DB_USER`, `DB_PASS` | Datenbankzugang |
| `STORAGE_PATH` | absoluter Pfad des Speicherverzeichnisses (außerhalb des Web-Verzeichnisses) |
| `MAX_UPLOAD` | größte erlaubte Datei in Bytes (größere Uploads: Antwort 413) |
| `AMICLOUD_ALLOW_HTTP` | erlaubt Klartext-HTTP, nur für einen lokalen Test mit `php -S`; in Produktion nie setzen |

Uploads kommen als Rumpf einer POST-Anfrage und werden im Speicher
verarbeitet. In der `php.ini` deshalb `post_max_size` mindestens so groß
wie `MAX_UPLOAD` wählen und `memory_limit` deutlich größer; für Uploads
über die Weboberfläche zusätzlich `upload_max_filesize`.

### 5. Datenbank anlegen

```sh
sudo mysql < /var/www/amicloud/sql/schema.sql
sudo mysql -e "CREATE USER 'amicloud'@'localhost' IDENTIFIED BY 'ein-langes-db-passwort';
  GRANT SELECT, INSERT, UPDATE, DELETE ON amicloud.* TO 'amicloud'@'localhost';"
```

`schema.sql` legt die Datenbank `amicloud` mit den Tabellen `users` und
`files` an und enthält bereits alle Spalten. Die Dateien
`sql/migrate_001.sql` bis `migrate_003.sql` sind **nur** für ältere,
bereits laufende Installationen gedacht; sie werden der Reihe nach
eingespielt (`mysql amicloud < migrate_00x.sql`) und sind nicht
wiederholbar – vorher mit dem in jeder Datei genannten
`SHOW COLUMNS …` prüfen, ob sie schon angewendet wurden.

### 6. Ersten Benutzer anlegen

Es gibt keine Benutzerverwaltung im Browser; Benutzer werden direkt in
der Datenbank angelegt. Das Passwort wird als bcrypt-Hash gespeichert:

```sh
php -r 'echo password_hash("geheim", PASSWORD_BCRYPT), PHP_EOL;'
# Ausgabe z. B.: $2y$10$abc...xyz
sudo mysql amicloud -e \
  "INSERT INTO users (username, pass_hash) VALUES ('bob', '\$2y\$10\$abc...xyz');"
```

Die `$`-Zeichen des Hashes in doppelten Anführungszeichen mit `\`
schützen (oder den Befehl interaktiv in `mysql` eingeben). Einen Benutzer
sperren: `UPDATE users SET active = 0 WHERE username = 'bob';`.

### 7. Apache neu laden und testen

```sh
sudo systemctl reload httpd
curl -u bob:geheim "https://amicloud.example.com/api.php?action=status"
# {"status":"ok","version":"0.1",...}
```

- API: `https://amicloud.example.com/api.php`
- Weboberfläche: `https://amicloud.example.com/web.php`
- Testsuite (legt ein temporäres Verzeichnis an und räumt es wieder weg):

  ```sh
  AC_SERVER=https://amicloud.example.com AC_USER=bob AC_PASS=geheim \
      sh scripts/test_api.sh
  ```

  Benötigt `curl` und `python3`.

Fehlersuche:

- **401 trotz richtigem Passwort** unter PHP-FPM: Apache reicht die
  Basic-Auth-Daten dann nicht weiter. Im `<Directory>`-Block
  `CGIPassAuth On` ergänzen.
- **500 bei jeder Anfrage**: `AllowOverride` erlaubt die `.htaccess` nicht,
  oder `config.php` fehlt. Apache-Fehlerlog prüfen.
- **403 „HTTPS required“**: Anfrage lief über `http://`.

### Lokaler Test ohne Apache

Für Entwicklung genügt der eingebaute PHP-Server und eine lokale MariaDB.
In der lokalen `config.php` `define('AMICLOUD_ALLOW_HTTP', true);`
setzen, dann:

```sh
php -S 127.0.0.1:8180 -t server/
AC_SERVER=http://127.0.0.1:8180 AC_USER=bob AC_PASS=geheim sh scripts/test_api.sh
```

Die Weboberfläche funktioniert so nicht (ihr Sitzungs-Cookie ist nur für
HTTPS freigegeben), die API schon.

---

## Amiga

### Voraussetzungen (Amiga)

| Was | Wofür |
|---|---|
| AmigaOS 3.x, 68000 oder besser | alle Programme (übersetzt für 68000, AmigaOS 3.x) |
| TCP/IP-Stack mit `bsdsocket.library` (Version 4 oder neuer), z. B. Roadshow, AmiTCP, Miami | alle Programme außer AmiCloudPrefs |
| **AmiSSL 5** (`amisslmaster.library`) mit installierten CA-Zertifikaten | alle Programme außer AmiCloudPrefs – die Verbindung zum Server läuft immer über HTTPS |
| **MUI 3.8** oder neuer (`muimaster.library` ab Version 19) | nur `amicloud-commander` |
| erreichbarer AmiCloud-Server | alle Programme außer AmiCloudPrefs |

Stack muss nicht eingestellt werden: `amicloud`, `amicloud-shell`,
`cloudsync`, `amicloud-commander` und AmiCloudPrefs fordern beim Start
selbst 64 KB an, der Handler bekommt 16 KB über seinen DOSDriver, der
Worker 64 KB beim Start durch den Handler.

Arbeitsspeicher: der `CLOUD:`-Handler hält eine geöffnete Datei
vollständig im Speicher (siehe Handbuch, Kapitel Handler). Für große
Dateien entsprechend freien Speicher einplanen oder `amicloud get`/`put`
benutzen, die direkt von/auf Platte übertragen.

### Installation mit dem Install-Skript

Die Installationsschublade `AmiCloud` (aus `AmiCloud.lha` oder erzeugt mit
`make dist-amiga`, siehe [BUILD.md](BUILD.md#amiga-installationsschublade))
enthält:

```
AmiCloud/
  Install-AmiCloud        Installationsskript
  Uninstall-AmiCloud      Deinstallationsskript
  ReadMe                  Kurzbeschreibung
  LICENSE                 GNU GPL v3
  C/                      alle Programme
  Docs/AmiCloud.guide     Handbuch (AmigaGuide)
  Examples/               amicloud.cfg.example, CLOUD.mountlist
  Storage/DOSDrivers/CLOUD   DOSDriver fuer CLOUD:
```

In einer Shell:

```
CD AmiCloud
Execute Install-AmiCloud
```

Das Skript fragt nacheinander, welche Teile installiert werden sollen
(Kommandozeilenprogramme, Commander, AmiCloudPrefs, `CLOUD:`-Handler),
kopiert sie an die Orte aus der Tabelle unten, legt eine
Beispielkonfiguration an, **ohne eine vorhandene Konfiguration zu
überschreiben**, bietet an, AmiCloudPrefs zum Eintragen der Zugangsdaten
zu starten und `CLOUD:` sofort zu mounten, und kopiert das Handbuch nach
`HELP:`, falls diese Zuweisung existiert.

### Manuelle Installation

| Datei in der Schublade | Ziel | Hinweis |
|---|---|---|
| `C/amicloud` | `C:` | Kommandozeilen-Client |
| `C/amicloud-shell` | `C:` | interaktive Shell |
| `C/cloudsync` | `C:` | einmaliger Ordnerabgleich |
| `C/amicloud-commander` | `SYS:Tools/` (oder beliebig) | braucht MUI; ein Icon wird nicht mitgeliefert |
| `C/AmiCloudPrefs` | `SYS:Prefs/` | Einstellungs-Editor |
| `C/amicloud-handler` | `L:` | Pfad steht im DOSDriver (`Handler = L:amicloud-handler`) |
| `C/amicloud-worker` | `L:` | **muss** `L:amicloud-worker` heißen, der Handler startet ihn von dort |
| `Storage/DOSDrivers/CLOUD` | `DEVS:DOSDrivers/` | wird beim Booten automatisch gemountet |
| `Examples/amicloud.cfg.example` | `ENVARC:amicloud.cfg` und `ENV:amicloud.cfg` | nur, wenn noch keine Konfiguration existiert |
| `Docs/AmiCloud.guide` | `HELP:` oder beliebig | Handbuch |

Als Shell-Befehle:

```
Copy C/amicloud C/amicloud-shell C/cloudsync C:
Copy C/amicloud-commander SYS:Tools/
Copy C/AmiCloudPrefs SYS:Prefs/
Copy C/amicloud-handler C/amicloud-worker L:
Copy Storage/DOSDrivers/CLOUD DEVS:DOSDrivers/
If NOT EXISTS ENVARC:amicloud.cfg
  Copy Examples/amicloud.cfg.example ENVARC:amicloud.cfg
EndIf
Copy ENVARC:amicloud.cfg ENV:amicloud.cfg
```

### Konfiguration (Amiga)

Alle Programme lesen dieselbe Datei `ENV:amicloud.cfg` (dauerhafte Kopie
in `ENVARC:amicloud.cfg`). Am einfachsten mit **AmiCloudPrefs**
bearbeiten: Server, Benutzer, Passwort eintragen und **Speichern**
wählen (schreibt nach `ENVARC:` und `ENV:`). Von Hand:

```
server=https://amicloud.example.com
user=bob
pass=geheim
```

Weitere Schlüssel (`basepath`, `verify_ssl`, `debug`, `gmt_offset`,
`dst`) erklärt [config-examples/README.md](../config-examples/README.md).
Auf dem Amiga sollten `gmt_offset` (Minuten zu UTC, MEZ = 60) und `dst`
zur eingestellten Uhrzeit passen, sonst stimmen Datumsangaben auf
`CLOUD:` und in `amicloud-shell` nicht.

Suchreihenfolge: `amicloud`, `amicloud-shell` und `cloudsync` nehmen
`-c <datei>`, sonst `ENV:amicloud.cfg`, sonst `amicloud.cfg` im aktuellen
Verzeichnis; der Commander `ENV:amicloud.cfg`, sonst `amicloud.cfg` im
aktuellen Verzeichnis; der Handler nur `ENV:amicloud.cfg`.

Test in der Shell:

```
amicloud version
amicloud list /
```

### CLOUD: mounten

Liegt `CLOUD` in `DEVS:DOSDrivers/`, wird das Laufwerk bei jedem
Systemstart gemountet. Sofort, ohne Neustart:

```
Mount CLOUD:
Dir CLOUD:
```

Beim Mounten startet der Handler `L:amicloud-worker`, der
`ENV:amicloud.cfg` liest. Die Konfiguration muss deshalb vorher
vollständig sein; spätere Änderungen wirken für `CLOUD:` erst nach einem
Neustart (ein sauberes Abmelden des Laufwerks gibt es nicht). Das Mounten
beim Booten funktioniert auch, wenn der TCP/IP-Stack erst später startet:
der Worker baut die Netzverbindung erst bei der ersten Anfrage auf.
Auf der Workbench erscheint das Laufwerk als `AmiCloud`.

### Deinstallation (Amiga)

Aus der Installationsschublade `Execute Uninstall-AmiCloud` ausführen –
das Skript löscht die installierten Programme, den DOSDriver und das
Handbuch und fragt vor dem Löschen der Konfiguration nach. Von Hand:

```
Delete C:amicloud C:amicloud-shell C:cloudsync
Delete SYS:Tools/amicloud-commander SYS:Prefs/AmiCloudPrefs
Delete L:amicloud-handler L:amicloud-worker
Delete DEVS:DOSDrivers/CLOUD
Delete HELP:AmiCloud.guide
Delete ENV:amicloud.cfg ENVARC:amicloud.cfg    ; nur wenn gewuenscht
```

Ein bereits gemountetes `CLOUD:` bleibt bis zum nächsten Neustart aktiv.

---

## PC

Auf dem PC gibt es `amicloud`, `amicloud-shell`, `cloudsync` und den
Qt-Commander `amicloud-commander`. Der `CLOUD:`-Handler und AmiCloudPrefs
sind Amiga-Programme.

### Installieren

Nach dem Bauen ([BUILD.md](BUILD.md#pc-macos-linux-windows)):

```sh
sudo make install                        # nach /usr/local/bin
make install PREFIX=$HOME/.local         # oder in das eigene Home
make install DESTDIR=/tmp/paket          # für Paketbau
```

`make install` kopiert alles aus `bin/pc/`. Alternativ die Programme von
Hand aus `bin/pc/` an einen Ort im `PATH` kopieren. Deinstallieren: die
Dateien `amicloud`, `amicloud-shell`, `cloudsync`, `amicloud-commander`
dort wieder löschen.

### Konfiguration (PC)

Die Kommandozeilenprogramme lesen `-c <datei>`, sonst `~/.amicloudrc`
(bei Windows-Builds ohne MSYS-Umgebung: `amicloud.cfg` im aktuellen
Verzeichnis). Der Qt-Commander liest `~/.amicloudrc` (Windows ohne
`HOME`: `%USERPROFILE%\.amicloudrc`); fehlen Angaben, fragt er sie in
einem Verbindungsdialog ab, speichert sie aber nicht.

```sh
cp config-examples/amicloud.cfg.example ~/.amicloudrc
chmod 600 ~/.amicloudrc          # enthaelt das Passwort im Klartext
$EDITOR ~/.amicloudrc
amicloud list /
```

`gmt_offset` und `dst` werden auf dem PC ignoriert.
