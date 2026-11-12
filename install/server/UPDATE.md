# AmiCloud-Server auf @VERSION@ aktualisieren

Diese Anleitung ist für einen **schon laufenden** AmiCloud-Server. Für eine
Neuinstallation gilt `INSTALL.md`; dort legt `schema.sql` die Datenbank
gleich vollständig an, Migrationen braucht es dann nicht.

Die Befehle gehen von den Pfaden aus `INSTALL.md` aus (`/var/www/amicloud`,
Datenbank `amicloud`, Webserver-Gruppe `apache`) und werden in diesem
Ordner ausgeführt, also dort, wo `server/` liegt.

## Welche Migrationen brauche ich?

Eine Migration ändert die Datenbank. Jede wird **genau einmal** eingespielt,
in der Reihenfolge ihrer Nummer, **vor** dem Kopieren der neuen Dateien.

| Server bisher | einspielen |
|---|---|
| 0.11 (oder neuer) | nichts |
| 0.3 bis 0.10 | `migrate_007.sql` |
| 0.1 oder 0.2 | `migrate_006.sql`, `migrate_007.sql` |
| älter als 0.1 | `migrate_001.sql` bis `migrate_007.sql` |

Wer nicht weiß, welcher Stand läuft: Jede Migration nennt oben in der Datei
eine Abfrage, die zeigt, ob sie schon eingespielt ist. Liefert sie eine
Zeile, ist die Migration schon drin:

| Migration | ist eingespielt, wenn das eine Zeile liefert |
|---|---|
| `migrate_001.sql` | `SHOW COLUMNS FROM files LIKE 'protection';` |
| `migrate_002.sql` | `SHOW COLUMNS FROM files LIKE 'checksum';` |
| `migrate_003.sql` | `SHOW COLUMNS FROM files LIKE 'revision';` |
| `migrate_004.sql` | `SHOW COLUMNS FROM users LIKE 'is_admin';` |
| `migrate_005.sql` | `SHOW CREATE TABLE files;` zeigt `path` als `varbinary(1024)` |
| `migrate_006.sql` | `SHOW COLUMNS FROM users LIKE 'tombstone_seq';` |
| `migrate_007.sql` | `SHOW COLUMNS FROM users LIKE 'long_names';` |

```sh
sudo mysql amicloud -e "SHOW COLUMNS FROM users LIKE 'long_names';"
```

Eine Migration lässt sich nicht zweimal einspielen; sie bricht dann mit
einer Fehlermeldung ab, ohne etwas zu verändern.

Fehlt `migrate_006.sql`, fallen die Programme ab 0.3 auf den vollen
Abgleich zurück und melden „Server zu alt für den schrittweisen Abgleich“;
Daten gehen dabei nicht verloren.

## Schritt für Schritt

1. **Sichern** – Dateien und Datenbank:
   ```sh
   sudo tar czf /root/amicloud-backup-$(date +%Y%m%d).tgz -C /var/www amicloud
   sudo mysqldump amicloud > /root/amicloud-db-$(date +%Y%m%d).sql
   ```
2. **Datenbank nachrüsten** – die Migrationen aus der Tabelle oben, als
   MariaDB-root (der Datenbankbenutzer `amicloud` darf kein `ALTER`), zum
   Beispiel von 0.10 auf @VERSION@:
   ```sh
   sudo mysql amicloud < server/sql/migrate_007.sql
   ```
3. **Dateien kopieren.** Das Archiv enthält keine `config.php`, die
   vorhandene bleibt erhalten. `server/.` mit Punkt, damit die
   `.htaccess`-Dateien mitkommen:
   ```sh
   sudo cp -r server/. /var/www/amicloud/
   sudo chown -R root:apache /var/www/amicloud
   sudo chmod -R u=rwX,g=rX,o= /var/www/amicloud
   sudo chmod 640 /var/www/amicloud/config.php
   ```
4. **Prüfen:**
   ```sh
   curl -u bob:geheim "https://amicloud.example.com/api.php?action=status"
   AC_SERVER=https://amicloud.example.com AC_USER=bob AC_PASS=geheim sh scripts/test_api.sh
   AC_SERVER=https://amicloud.example.com AC_USER=bob AC_PASS=geheim python3 scripts/test_changes.py
   ```
   Die Antwort von `status` enthält mit den Dateien von @VERSION@ das Feld
   `name_max` (30 im Amiga-Modus, sonst 255). Die Tests legen einen eigenen
   Ordner an und löschen ihn danach wieder.

## Was sich mit @VERSION@ ändert

**Amiga-Modus für Dateinamen (`migrate_007.sql`).** Das klassische
Amiga-Dateisystem (FFS/OFS) speichert höchstens 30 Zeichen pro Name und
kürzt längere ohne Warnung. Ein Konto im Amiga-Modus legt deshalb keine
**neuen** Datei- oder Ordnernamen über 30 Zeichen mehr an – weder über die
Programme noch über die Weboberfläche (HTTP 400). Vorhandene Dateien sind
nie betroffen: Lesen, Überschreiben, Löschen, Verschieben unter gleichem
Namen und der Abgleich bestehender Dateien gehen weiter.

- Die Migration setzt den Amiga-Modus für **alle** Konten – **außer** für
  die, die schon Dateien mit Namen über 30 Zeichen haben. Deren bisherige
  Arbeitsweise (zum Beispiel ein Desktop-Abgleich am PC) läuft unverändert.
- Neue Konten starten im Amiga-Modus.
- Umschalten pro Konto: in der Weboberfläche unter *Persönliche
  Einstellungen → Dateinamen*, oder als Administrator:
  ```sh
  sudo -u apache php /var/www/amicloud/bin/amicloud-admin.php user:longnames bob on
  ```
- Ohne `migrate_007.sql` verhält sich der neue Server wie bisher (keine
  Grenze); das Häkchen in den Einstellungen meldet dann, dass die Migration
  fehlt.

**Zusammenspiel der Versionen.** Ältere Programme funktionieren mit dem
neuen Server; im Amiga-Modus endet nur das Anlegen eines zu langen neuen
Namens mit einer Fehlermeldung. Neue Programme mit einem älteren Server
kennen keine Grenze und arbeiten wie bisher.

## Zurück zur alten Fassung

Die Sicherungen aus Schritt 1 zurückspielen:

```sh
sudo tar xzf /root/amicloud-backup-JJJJMMTT.tgz -C /var/www
sudo mysql amicloud < /root/amicloud-db-JJJJMMTT.sql
```

Die zusätzliche Spalte aus `migrate_007.sql` stört eine ältere
Server-Fassung nicht; wer nur die Dateien zurückspielt, kann die Datenbank
so lassen.
