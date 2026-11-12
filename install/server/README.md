# AmiCloud-Server @VERSION@

Server für AmiCloud: REST-API für die Amiga-, PC- und Mac-Programme und
eine Weboberfläche im Stil von ownCloud. Läuft mit Apache, PHP (ab 7.4) und
MariaDB oder MySQL.

## Inhalt des Archivs

| Pfad | Inhalt |
|---|---|
| `server/` | Dateien für das Web-Verzeichnis des Servers |
| `UPDATE.md` | eine bestehende Installation aktualisieren (Datenbank-Migrationen) |
| `api/API.md` | Referenz der REST-API |
| `scripts/test_api.sh`, `scripts/test_changes.py` | Tests gegen einen laufenden Server (brauchen `curl` und `python3`) |
| `LICENSE` | GPL-3.0-or-later |

Alle Befehle unten werden in diesem Ordner ausgeführt (dort, wo `server/`
liegt). `config.php` ist nicht im Archiv, sie wird aus
`server/config.php.example` angelegt.

## Neu installieren

Die Schritte 1 bis 7 im Abschnitt „Server“ weiter unten.

## Aktualisieren

Eine schon laufende Installation aktualisiert man nach **`UPDATE.md`** in
diesem Ordner: sichern, die nötigen Datenbank-Migrationen einspielen (eine
Tabelle dort sagt, welche), Dateien kopieren, prüfen – und was sich mit
dieser Version am Verhalten ändert.
