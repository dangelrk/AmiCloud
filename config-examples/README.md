# Konfigurationsbeispiele

Ein Beispiel pro Konfigurationsformat, das AmiCloud verwendet. Bei
`make dist-amiga` landet dieser Ordner als `Examples/` in der
Amiga-Installationsschublade.

| Datei | Ablage im Betrieb | Gelesen von | Bearbeitet mit |
|---|---|---|---|
| [amicloud.cfg.example](amicloud.cfg.example) | Amiga: `ENV:amicloud.cfg` (dauerhaft `ENVARC:amicloud.cfg`); PC: `~/.amicloudrc` | `amicloud`, `amicloud-shell`, `cloudsync`, `amicloud-commander` (MUI und Qt), `amicloud-worker` (Backend des `CLOUD:`-Handlers) | von Hand oder mit **AmiCloudPrefs** (Amiga, `SYS:Prefs`) |
| [CLOUD.mountlist](CLOUD.mountlist) | beliebig, z. B. an `DEVS:MountList` anhängen | AmigaDOS `Mount CLOUD: FROM …` → `L:amicloud-handler` | von Hand (normalerweise keine Änderung nötig) |
| `../server/config.php.example` | `config.php` im Web-Verzeichnis des Servers | AmiCloud-Server (`api.php`, `web.php`) | von Hand, siehe [docs/INSTALL.md](../docs/INSTALL.md#server) |

`CLOUD.mountlist` ist ein klassischer Mountlist-Eintrag (`CLOUD:` … `#`)
mit denselben Werten wie der DOSDriver `handler/cloudhandler/mount/CLOUD`.
Den DOSDriver legt `make dist-amiga` als `Storage/DOSDrivers/CLOUD` in die
Installationsschublade; das Install-Skript kopiert ihn nach
`DEVS:DOSDrivers/`. Die Mountlist ist die Alternative ohne DOSDriver:
`Mount CLOUD: FROM Examples/CLOUD.mountlist`. Änderungen an den Werten
bitte an beiden Stellen nachziehen.

## Suchreihenfolge der Konfigurationsdatei

| Programm | Plattform | Gesucht wird |
|---|---|---|
| `amicloud`, `amicloud-shell`, `cloudsync` | Amiga | `-c <datei>`, sonst `ENV:amicloud.cfg`, sonst `amicloud.cfg` im aktuellen Verzeichnis |
| `amicloud`, `amicloud-shell`, `cloudsync` | macOS/Linux (und MSYS2-Umgebung "MSYS") | `-c <datei>`, sonst `~/.amicloudrc` |
| `amicloud`, `amicloud-shell`, `cloudsync` | Windows (native MinGW-Builds) | `-c <datei>`, sonst `amicloud.cfg` im aktuellen Verzeichnis |
| `amicloud-commander` (MUI) | Amiga | `ENV:amicloud.cfg`, sonst `amicloud.cfg` im aktuellen Verzeichnis |
| `amicloud-commander` (Qt) | PC | `$HOME/.amicloudrc` (Windows ohne `HOME`: `%USERPROFILE%\.amicloudrc`); fehlen Server/Benutzer/Passwort, fragt ein Verbindungsdialog danach (ohne zu speichern) |
| `amicloud-worker` (`CLOUD:`) | Amiga | nur `ENV:amicloud.cfg`, einmal beim Start des Workers |
| AmiCloudPrefs | Amiga | liest `ENV:amicloud.cfg`, sonst `ENVARC:amicloud.cfg` |

Kommandozeilenoptionen (`-s`, `-u`, `-p`, `-d`, `-k`) überschreiben die
Werte aus der Datei.

## Schlüssel in `amicloud.cfg`

Format: eine Zeile `schluessel=wert`, `#` am Zeilenanfang leitet einen
Kommentar ein. Parser: `core/libcloudcore/src/config.c`.

| Schlüssel | Standard | Bedeutung |
|---|---|---|
| `server` | – (Pflicht) | Basis-URL des Servers ohne `/api.php`, z. B. `https://amicloud.example.com` |
| `user` / `username` | – (Pflicht) | Benutzername |
| `pass` / `password` | – (Pflicht) | Passwort (steht im Klartext in der Datei) |
| `basepath` | `/` | Startverzeichnis auf dem Server |
| `verify_ssl` | `1` | `1` = TLS-Zertifikat prüfen, `0` = nicht prüfen (nur zum Testen) |
| `debug` | leer | Nur `amicloud-worker`: Ziel für das HTTP/TLS-Protokoll des `CLOUD:`-Handlers, z. B. `T:amicloud.log` oder `CON:0/0/640/200/AmiCloud Debug/CLOSE` |
| `gmt_offset` | `0` | Nur Amiga (`amicloud-shell`, `amicloud-worker`): Abstand der Systemuhr zu UTC in Minuten (MEZ = 60) |
| `dst` | `0` | Nur Amiga: `1` = Sommerzeit aktiv (addiert 60 Minuten) |

## AmiCloudPrefs

Der Einstellungs-Editor (GadTools, `SYS:Prefs/AmiCloudPrefs`) bearbeitet
alle Schlüssel oben und folgt der üblichen Workbench-Prefs-Semantik:
**Benutzen** schreibt nur `ENV:amicloud.cfg` (gilt bis zum Neustart),
**Speichern** schreibt `ENVARC:amicloud.cfg` und `ENV:amicloud.cfg`,
**Abbrechen** schreibt nichts. Unterscheiden sich `ENV:` und `ENVARC:`,
zeigt die Statuszeile einen Hinweis.

Der `CLOUD:`-Handler liest die Datei nur beim Start von
`amicloud-worker` – Änderungen wirken dort erst nach einem Neustart des
Amiga.
