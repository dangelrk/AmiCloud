# ADR: Multiuser-Architektur

## Ziel

Die erste Version richtet sich an einen einzelnen Benutzer. Die
Architektur soll jedoch von Anfang an mehrere Benutzer unterstützen
können, ohne später grundlegende Umbauten zu erfordern.

## Grundsatz

Version 1 bleibt funktional ein Einbenutzersystem.

Mehrbenutzerfähigkeit wird ausschließlich im Datenmodell vorbereitet.

## Server

Der Server verwaltet:

-   Benutzer
-   Sitzungen
-   Dateien
-   Verzeichnisse

Jede Datei besitzt einen Besitzer (owner_id).

Beispiel:

-   id
-   owner_id
-   path
-   size
-   md5
-   mtime
-   revision
-   upload_time

Zu Beginn existiert lediglich ein Benutzer.

## Authentifizierung

Login dient ausschließlich der Authentifizierung.

Nach erfolgreichem Login erhält der Client eine Session oder ein Token.

Alle API-Aufrufe arbeiten automatisch im Kontext dieses Benutzers.

Der Client übergibt keine Benutzer-ID bei Dateioperationen.

## API

Die API bleibt benutzerneutral.

Beispiele:

GET /files POST /upload POST /mkdir DELETE /file

Der Server entscheidet anhand der Session, auf welchen Datenbestand
zugegriffen wird.

## Nicht Bestandteil von Version 1

-   Freigaben
-   Gruppen
-   ACLs
-   Öffentliche Links
-   Gemeinsame Ordner
-   Quotas

Diese Funktionen bleiben ausdrücklich zukünftigen Versionen vorbehalten.

## Datenbank

Empfohlene Tabellen:

-   users
-   sessions
-   files
-   directories
-   settings

Optional:

-   audit_log
-   deleted_files

## Installation

Der Server soll möglichst einfach installierbar sein.

Ziel:

1.  PHP-Dateien kopieren.
2.  SQLite oder MySQL konfigurieren.
3.  install.php ausführen.
4.  Ersten Benutzer anlegen.
5.  Fertig.

## Langfristige Ziele

-   Mehrere unabhängige Benutzer
-   Plattformunabhängige Cloud API
-   Austauschbares Server-Backend (PHP, Go, Rust usw.)
-   Kompatibilität aller Clients (AmigaOS, Windows, Linux, macOS)

## Architekturprinzip

Der Server verwaltet Daten.

Der Client verwaltet Synchronisation.

Der Server entscheidet über Authentifizierung und Berechtigungen.

Der Client entscheidet über Upload, Download, Konflikte und
Synchronisationslogik.
