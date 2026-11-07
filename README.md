# AmiCloud

*AmiCloud is a self-hosted cloud storage for AmigaOS 3.x and the PC: a small
PHP/MariaDB server plus clients – a command-line tool, an interactive shell,
a one-shot folder sync, a two-pane file manager (MUI on the Amiga, Qt 6 on
macOS/Linux/Windows) and an AmigaOS DOS handler that mounts the cloud as the
drive `AMICLOUD:`. The documentation is in German.*

> **Status:** experimental. Developed with the help of Claude (an AI assistant
> by Anthropic), primarily for the author's own use. Try it with copies of
> your data first; use at your own risk.

AmiCloud ist ein eigener Cloud-Speicher für den Amiga. Ein schlanker
PHP-Server hält die Dateien, die Programme greifen über HTTPS darauf zu –
auf dem Amiga (AmigaOS 3.x, bsdsocket + AmiSSL 5) ebenso wie auf dem PC
(macOS, Linux, Windows). Auf dem Amiga erscheint die Cloud auf Wunsch als
ganz normales Laufwerk `AMICLOUD:`, mit Protection-Bits, Dateikommentaren und
Datumsangaben.

## Komponenten

| Programm | Plattform | Zweck |
|---|---|---|
| `amicloud` | Amiga + PC | Kommandozeilen-Client: ein Befehl je Aufruf (`list`, `get`, `put`, `del`, `info`, `mkdir`, `rename`, `search`, `quota`, `version`) |
| `amicloud-shell` | Amiga + PC | interaktive Sitzung mit `cd`, `ls`, `get`/`put` (auch rekursiv und mit Mustern), `md5`, `search` … |
| `amicloud-sync` | Amiga + PC | einmaliger Zwei-Wege-Abgleich eines lokalen Ordners mit einem Cloud-Ordner |
| `amicloud-commander` | Amiga (MUI) + PC (Qt 6) | Dateimanager mit zwei Panels, jedes wahlweise lokal oder Cloud |
| `AmiCloudPrefs` | Amiga | Einstellungs-Editor für `amicloud.cfg` |
| `AMICLOUD:`-Handler (`amicloud-handler` + `amicloud-worker`) | Amiga | bindet die Cloud als AmigaDOS-Laufwerk ein |
| Server (`server/`) | Linux, Apache, PHP ≥ 7.4, MariaDB | REST-API (`api.php`) und Weboberfläche (`web.php`) im Stil von ownCloud: Benutzerverwaltung, App-Passwörter, Speicherkontingente, öffentliche Links |

## Schnellstart

```sh
git clone --recurse-submodules https://github.com/dangelrk/AmiCloud.git
cd AmiCloud
make pc                          # amicloud, amicloud-shell, amicloud-sync -> bin/pc/
make qt                          # Qt-Commander -> bin/pc/ (Qt 6 + CMake)
export VBCC=/pfad/zu/vbcc
make amiga                       # alle Amiga-Programme -> bin/amiga/
make dist-amiga                  # Installationsschublade build/dist/AmiCloud/
make release                     # build/release/AmiCloud-<version>.lha + .readme
```

Dann auf dem Amiga in der Schublade das Icon `Install-AmiCloud` doppelt
anklicken (Commodore-Installer; ohne Installer `Execute Install-AmiCloud.shell`), in
AmiCloudPrefs Server und Zugangsdaten eintragen und loslegen:

```
amicloud list /
Mount AMICLOUD:
Dir AMICLOUD:
```

## Dokumentation

| Dokument | Inhalt |
|---|---|
| [docs/INSTALL.md](docs/INSTALL.md) | Installation: Server (Apache/PHP/MariaDB), Amiga, PC |
| [docs/BUILD.md](docs/BUILD.md) | Bauen: PC (macOS/Linux/Windows, CLI + Qt), Amiga (Cross-Compile mit vbcc, nativ mit `Build-Amiga`), Emulator |
| [docs/user/AmiCloud.guide](docs/user/AmiCloud.guide) | Handbuch im AmigaGuide-Format (Latin-1) |
| [docs/user/AmiCloud-Handbuch.html](docs/user/AmiCloud-Handbuch.html) | dasselbe Handbuch als HTML |
| [docs/api/API.md](docs/api/API.md) | REST-API-Referenz des Servers |
| [config-examples/README.md](config-examples/README.md) | Konfigurationsdateien, Ablageorte, Schlüssel |
| [handler/cloudhandler/README.md](handler/cloudhandler/README.md) | Innenleben des `AMICLOUD:`-Handlers (englisch) |
| [docs/adr/](docs/adr/), [docs/design/](docs/design/) | Architekturentscheidungen und historische Planungsdokumente |
| [TODO.md](TODO.md) | bekannte offene Punkte |

## Verzeichnisse

| Verzeichnis | Inhalt |
|---|---|
| `apps/` | ein Unterverzeichnis je Programm |
| `handler/cloudhandler/` | `AMICLOUD:`-Handler (nur Amiga) mit DOSDriver `mount/AMICLOUD` |
| `server/` | PHP-Server |
| `core/` | [AmiCloudCore](https://github.com/dangelrk/AmiCloudCore) als Git-Submodul: gemeinsame C-Bibliotheken und Buildlogik |
| `config-examples/` | Beispielkonfiguration und Mountlist |
| `install/amiga/` | Install-/Uninstall-Skript und ReadMe der Amiga-Schublade |
| `scripts/` | `test_api.sh` (API-Testsuite); Emulator-Test per `make fsuae` (siehe BUILD.md) |
| `Build-Amiga` | erzeugtes AmigaDOS-Skript zum nativen Bauen auf dem Amiga |

## Schwesterprojekte

- [AmiCloudCore](https://github.com/dangelrk/AmiCloudCore) – gemeinsame Bibliotheken (dieses Projekt bindet es als `core/` ein)
- [AmiOwnCloud](https://github.com/dangelrk/AmiOwnCloud) – Zugriff auf Standard-OwnCloud/Nextcloud per WebDAV (`OWNCLOUD:`-Handler, Sync-Commodity)
- [AmiVCS](https://github.com/dangelrk/AmiVCS) – Versionsverwaltung mit eigenem Server

## Lizenz

AmiCloud ist freie Software: Copyright (C) 2026 Ronny Krueger, lizenziert
unter der GNU General Public License, Version 3 oder (nach Wahl) jeder
späteren Version (`GPL-3.0-or-later`). Der vollständige Text steht in
[LICENSE](LICENSE). Das Programm wird ohne jede Gewährleistung
bereitgestellt.

Der in AmiCloudCore enthaltene JSON-Parser
[Parson](https://github.com/kgabis/parson) steht unter der MIT-Lizenz
(siehe `THIRD-PARTY-NOTICES.md` in
[AmiCloudCore](https://github.com/dangelrk/AmiCloudCore)).
