# AmiCloud-Server @VERSION@

Server für AmiCloud: REST-API für die Amiga-, PC- und Mac-Programme und
eine Weboberfläche im Stil von ownCloud. Läuft mit Apache, PHP (ab 7.4) und
MariaDB oder MySQL.

## Inhalt des Archivs

| Pfad | Inhalt |
|---|---|
| `server/` | Dateien für das Web-Verzeichnis des Servers |
| `api/API.md` | Referenz der REST-API |
| `scripts/test_api.sh`, `scripts/test_changes.py` | Tests gegen einen laufenden Server (brauchen `curl` und `python3`) |
| `LICENSE` | GPL-3.0-or-later |

Alle Befehle unten werden in diesem Ordner ausgeführt (dort, wo `server/`
liegt). `config.php` ist nicht im Archiv, sie wird aus
`server/config.php.example` angelegt.

## Neu installieren

Die Schritte 1 bis 7 im Abschnitt „Server“ weiter unten.

## Aktualisieren

1. **Sichern** (Pfade wie bei der Installation):
   ```sh
   sudo tar czf /root/amicloud-backup-$(date +%Y%m%d).tgz -C /var/www amicloud
   sudo mysqldump amicloud > /root/amicloud-db-$(date +%Y%m%d).sql
   ```
2. **Datenbank nachrüsten**, und zwar *vor* dem Kopieren der Dateien. Die
   noch fehlenden Dateien `server/sql/migrate_00x.sql` der Reihe nach als
   MariaDB-root einspielen (der Benutzer `amicloud` darf kein `ALTER`):
   ```sh
   sudo mysql amicloud < server/sql/migrate_006.sql
   ```
   - Von 0.1 oder 0.2 auf @VERSION@: nur `migrate_006.sql`.
   - Ältere Installationen von vor 0.1 brauchen zuerst `migrate_001.sql` bis
     `migrate_005.sql`.

   Die Migrationen lassen sich nicht wiederholen. In jeder Datei steht oben
   ein `SHOW COLUMNS …`, mit dem man prüft, ob sie schon eingespielt ist.
3. **Dateien kopieren.** Das Archiv enthält keine `config.php`, die
   vorhandene bleibt also erhalten. `server/.` mit Punkt, damit die
   `.htaccess`-Dateien mitkommen:
   ```sh
   sudo cp -r server/. /var/www/amicloud/
   sudo chown -R root:apache /var/www/amicloud
   sudo chmod -R u=rwX,g=rX,o= /var/www/amicloud
   sudo chmod 640 /var/www/amicloud/config.php
   ```
4. **Testen:**
   ```sh
   curl -u bob:geheim "https://amicloud.example.com/api.php?action=status"
   AC_SERVER=https://amicloud.example.com AC_USER=bob AC_PASS=geheim sh scripts/test_api.sh
   AC_SERVER=https://amicloud.example.com AC_USER=bob AC_PASS=geheim python3 scripts/test_changes.py
   ```
   Beide Tests legen einen eigenen Ordner an und löschen ihn danach wieder.

Der Abgleich der Programme ab 0.3 braucht `migrate_006.sql`. Gegen einen
älteren Server fallen sie auf den vollen Abgleich zurück und melden „Server
zu alt für den schrittweisen Abgleich“; Daten gehen dabei nicht verloren.
