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
| `index.php`, `logo.svg` | Startseite (`/`) mit Logo und Link zur Weboberfläche; per `DirectoryIndex` in `.htaccess` |
| `api.php` | REST-API für alle Client-Programme (Referenz: [api/API.md](api/API.md)) |
| `web.php`, `web.css`, `web.js` | Weboberfläche im Stil von ownCloud: Dateien (Hochladen per Drag & Drop, Umbenennen, Verschieben, Löschen, ZIP), Suche, Details mit Amiga-Kommentar, öffentliche Links, persönliche Einstellungen mit App-Passwörtern, Benutzerverwaltung für Admins |
| `share.php` | öffentliche Linkseite (Download ohne Konto, optional mit Passwort/Ablaufdatum) |
| `bin/amicloud-admin.php` | Verwaltung auf der Kommandozeile (Benutzer anlegen, Passwort, Admin, Kontingent …); `bin/` ist für den Browser gesperrt |
| `includes/` | PHP-Klassen (per `.htaccess` für Browser gesperrt) |
| `sql/schema.sql` | Datenbankschema für eine Neuinstallation (`sql/` per `.htaccess` für Browser gesperrt) |
| `sql/migrate_001.sql` … `migrate_005.sql` | Nachrüsten älterer Installationen |
| `config.php.example` | Vorlage für `config.php` |
| `.user.ini` | Upload-Grenzen für PHP-FPM (100 MB, passend zu `MAX_UPLOAD`) |
| `.htaccess` | sperrt `config.php`, `config.php.example` und `.user.ini` |

### Voraussetzungen (Server)

- Linux-Server mit **Apache 2.4** (für die `.htaccess`-Dateien wird
  `AllowOverride All` benötigt)
- **PHP 7.4 oder neuer** (PHP 8 geht ebenso) mit den Erweiterungen
  `pdo_mysql`, `fileinfo` und `mbstring`; `zip` für ZIP-Downloads in der
  Weboberfläche (optional)
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
# Code gehört root, Apache darf nur lesen; config.php (Passwort) nur
# für root und die Gruppe apache; schreiben darf Apache nur im Storage:
sudo chown -R root:apache /var/www/amicloud
sudo chmod -R u=rwX,g=rX,o= /var/www/amicloud
sudo chmod 640 /var/www/amicloud/config.php
sudo chown -R apache:apache /var/amicloud-storage
sudo chmod 750 /var/amicloud-storage
```

Wichtig: `server/.` mit Punkt kopieren, nicht `server/*` – das Sternchen
lässt die `.htaccess`-Dateien weg, und dann wären `config.php` und
`includes/` im Browser abrufbar. Wer vom eigenen Rechner hochlädt:
`scp -r server/. benutzer@amicloud.example.com:/var/www/amicloud/`.

Das Speicherverzeichnis (`/var/amicloud-storage`) muss **außerhalb** des
Web-Verzeichnisses liegen. Jeder Benutzer bekommt darin einen eigenen
Unterordner mit seinem Benutzernamen (beim Anlegen des Benutzers).

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
verarbeitet. `post_max_size` muss deshalb mindestens so groß wie
`MAX_UPLOAD` sein und `memory_limit` deutlich größer; für Uploads über
die Weboberfläche zusätzlich `upload_max_filesize`. Unter **PHP-FPM**
erledigt das die mitgelieferte `.user.ini` (wirkt nur für das
AmiCloud-Verzeichnis, PHP liest sie spätestens nach 5 Minuten neu);
unter `mod_php` dieselben Werte in die `php.ini` oder per `php_value`
in den Virtual Host eintragen.

### 5. Datenbank anlegen

```sh
sudo mysql < /var/www/amicloud/sql/schema.sql
sudo mysql -e "CREATE USER 'amicloud'@'localhost' IDENTIFIED BY 'ein-langes-db-passwort';
  GRANT SELECT, INSERT, UPDATE, DELETE ON amicloud.* TO 'amicloud'@'localhost';"
```

`schema.sql` legt die Datenbank `amicloud` mit allen Tabellen (`users`,
`files`, `app_passwords`, `shares`, `auth_failures`) an. Die Dateien
`sql/migrate_001.sql` bis `migrate_005.sql` sind **nur** für ältere,
bereits laufende Installationen gedacht; sie werden der Reihe nach
eingespielt (`mysql amicloud < migrate_00x.sql`) und sind nicht
wiederholbar – vorher mit dem in jeder Datei genannten
`SHOW COLUMNS …` prüfen, ob sie schon angewendet wurden.

### 6. Ersten Administrator anlegen

Den ersten Benutzer legt man auf der Kommandozeile an, als Benutzer des
Webservers (dann gehört sein Speicherordner gleich Apache); das Passwort
wird ohne Echo abgefragt:

```sh
cd /var/www/amicloud
sudo -u apache php bin/amicloud-admin.php user:add --admin --display="Bob Beispiel" bob
```

Alle weiteren Benutzer verwaltet der Administrator in der Weboberfläche
(Benutzermenü → *Benutzer*): anlegen, Kontingent setzen, Passwort
zurücksetzen, deaktivieren, löschen. Dasselbe geht mit dem
Kommandozeilenwerkzeug:

| Befehl | Wirkung |
|---|---|
| `user:list` | alle Benutzer mit Belegung und letzter Anmeldung |
| `user:add [--admin] [--quota=5G] [--display=…] [--email=…] <name>` | Benutzer anlegen |
| `user:passwd <name>` | Passwort setzen (beendet dessen Browser-Sitzungen) |
| `user:admin <name> on\|off` | Administratorrecht vergeben/entziehen |
| `user:quota <name> <größe\|unbegrenzt>` | Kontingent, z. B. `500M`, `5G` |
| `user:disable <name>` / `user:enable <name>` | Anmeldung sperren/erlauben |
| `user:delete <name>` | Benutzer **samt allen Dateien** löschen (Name zur Bestätigung eintippen) |

Mit `--password-stdin` liest `user:add`/`user:passwd` das Passwort aus
einer Zeile der Standardeingabe (für Skripte). Benutzernamen bestehen aus
Kleinbuchstaben, Ziffern, `.`, `-` und `_` und lassen sich später nicht
ändern (sie bilden den Speicherordner); änderbar ist der Anzeigename.
Der letzte aktive Administrator kann weder gelöscht noch deaktiviert noch
herabgestuft werden.

**Nachrüsten einer bestehenden Installation:** `migrate_004.sql` und `migrate_005.sql` als
MariaDB-root einspielen (der Datenbankbenutzer `amicloud` darf kein
`ALTER`), danach einen vorhandenen Benutzer zum Administrator machen:
`sudo -u apache php bin/amicloud-admin.php user:admin bob on`.

Für die Clients empfiehlt sich je Gerät ein **App-Passwort** statt des
Kontopassworts (Weboberfläche → *Persönliche Einstellungen → Geräte &
App-Passwörter*); es lässt sich einzeln widerrufen.

### 7. Apache neu laden und testen

```sh
sudo systemctl reload httpd
curl -u bob:geheim "https://amicloud.example.com/api.php?action=status"
# {"status":"ok","version":"0.1",...}
```

- Startseite: `https://amicloud.example.com/`
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
- **429 „Too many failed logins“**: Nach 5 Fehlversuchen (pro Benutzer und
  IP) sperrt der Server 15 Minuten lang. Oft ein Gerät mit altem Passwort
  in der Konfiguration.

### Lokaler Test ohne Apache

Für Entwicklung genügt der eingebaute PHP-Server und eine lokale MariaDB.
In der lokalen `config.php` `define('AMICLOUD_ALLOW_HTTP', true);`
setzen, dann:

```sh
php -S 127.0.0.1:8180 -t server/
AC_SERVER=http://127.0.0.1:8180 AC_USER=bob AC_PASS=geheim sh scripts/test_api.sh
```

Mit `AMICLOUD_ALLOW_HTTP` funktionieren API und Weboberfläche auch über
`http://` (das Sitzungs-Cookie ist dann nicht als `Secure` markiert – nur
lokal verwenden).

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
  THIRD-PARTY-NOTICES     Lizenz der Fremdkomponente Parson (MIT)
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
