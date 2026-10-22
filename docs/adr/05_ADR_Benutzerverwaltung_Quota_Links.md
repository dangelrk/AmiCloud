# ADR 05: Mehrbenutzerbetrieb, Speicherkontingente und öffentliche Links

Stand: 22.10.2026 · schreibt [ADR 04](04_ADR_Multiuser_Architektur.md) fort ·
Plan: [docs/design/user-management-plan.md](../design/user-management-plan.md)

## Kontext

ADR 04 hat Mehrbenutzerfähigkeit nur im Datenmodell vorbereitet und
Freigaben, öffentliche Links und Quotas ausdrücklich für spätere Versionen
zurückgestellt. Benutzer wurden per SQL-`INSERT` angelegt, die
Weboberfläche war eine einzelne Tabellenansicht. Ziel ist jetzt ein Dienst,
der sich im Browser wie ownCloud bedient.

## Entscheidung

1. **Benutzerverwaltung** in der Weboberfläche (Administratoren) und im
   Kommandozeilenwerkzeug `server/bin/amicloud-admin.php`. Den ersten
   Administrator legt nur die Kommandozeile an – kein Web-Installer, weil
   eine frisch deployte, öffentlich erreichbare Instanz sonst übernommen
   werden könnte.
2. **Benutzername unveränderlich** (wie die ownCloud-UID), denn er bildet
   den Speicherordner `STORAGE_PATH/<name>`. Änderbar ist der Anzeigename.
3. **App-Passwörter** je Gerät, per Basic Auth gleichwertig zum
   Kontopasswort, einzeln widerrufbar. Gespeichert als SHA-256 (hohe
   Entropie, kein bcrypt bei jedem Amiga-Request).
4. **Speicherkontingente** je Benutzer; `status` meldet Belegung und Größe
   (bei unbegrenzt: belegt + freier Plattenplatz), `upload` lehnt mit 507
   ab. Der `CLOUD:`-Handler zeigt die echte Belegung bei `Info`.
5. **Öffentliche Links** auf Dateien und Ordner (Passwort, Ablaufdatum
   optional), ausgeliefert nur als Download.
6. **Login-Bremse** (429) für API, Web und Link-Passwörter.

Die REST-API bleibt benutzerneutral (ADR 04): Clients übergeben nie eine
Benutzer-ID, Benutzerverwaltung läuft nicht über die API. Alle
API-Erweiterungen sind additiv.

## Weiterhin nicht Bestandteil

Gruppen, gemeinsame Ordner zwischen Benutzern, ACLs, Papierkorb und
Versionen (der Sync arbeitet mit Tombstones), LDAP, Zwei-Faktor-Anmeldung,
E-Mail-Versand.

## Folgen

- Schema-Erweiterung `sql/migrate_004.sql` (additiv).
- `AmiCloudCore` bekommt `cmd_status_ex()`/`AcStatus` und `ac_format_kb()`
  – rein additiv, AmiOwnCloud und AmiVCS sind nicht betroffen.
- Der Handler fragt die Quota nie selbst ab (das Backend hat keinen
  Timeout), sondern bekommt sie mit Verzeichnislisten des Workers geliefert.
