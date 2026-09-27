# Refactoring-Roadmap

## Phase 1

Bestehende Anwendungen stabilisieren (amicloud, amicloud-shell,
amicloud-commander, Handler, Server).

## Phase 2 -- libcloudhttp

HTTP/HTTPS, Login, JSON, Upload, Download, Delete, Rename, Mkdir, Rmdir
und Directory Listing in eine gemeinsame Bibliothek auslagern.

## Phase 3 -- libcloudfs

Gemeinsame Dateisystemfunktionen (Scannen, MD5, mtime, Größe).

## Phase 4 -- libcloudsync

Aus Local State, Remote State und Last Sync State einen Sync-Plan
erzeugen. Keine GUI, kein HTTP.

## Phase 5 -- Sync Commodity

Eigenständiger Hintergrunddienst zum Synchronisieren definierter Ordner.

## Phase 6

Komfortfunktionen (Konflikte, Status, Zeitplanung, Auto-Sync).
