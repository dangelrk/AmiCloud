# ToDo

## Zeitzone wird von `amicloud-sync` und dem MUI-Commander ignoriert (Amiga)

**Status:** offen, bekannt seit 27.09.2026

Die Amiga-Systemuhr kennt keine Zeitzone. `libcloudfs` rechnet lokale
Datumsangaben deshalb mit `gmt_offset`/`dst` aus der Konfiguration in
UTC um – aber nur, wenn das Programm die Werte vorher mit
`localfs_set_tz()` setzt (`core/libcloudfs/src/localfs_amiga.c`).

| Programm | ruft `localfs_set_tz()` auf |
|---|---|
| `amicloud-shell` | ja (`apps/amicloud-shell/src/shell.c`) |
| `amicloud-sync` | **nein** |
| `amicloud-commander` (MUI) | **nein** |
| `amicloud` (CLI) | nicht nötig – vergleicht keine lokalen Zeiten |
| `AMICLOUD:`-Handler | nicht betroffen – rechnet im Worker selbst mit `gmt_offset`/`dst` |

**Auswirkung:** Auf dem Amiga behandeln `amicloud-sync` und der Commander
lokale Dateizeiten so, als stünde die Uhr auf UTC. `gmt_offset=` und
`dst=` in `amicloud.cfg` bleiben wirkungslos. Beim Abgleich können
Dateien dadurch als geändert gelten, obwohl sie es nicht sind, oder
Konflikte falsch entschieden werden. Die im Commander angezeigten
lokalen Zeiten sind um den Offset verschoben. Auf dem PC gibt es das
Problem nicht, dort kennt das Betriebssystem die Zeitzone.

**Lösung (Vorschlag):** In `apps/amicloud-sync/src/main.c` und
`apps/amicloud-commander-mui/src/main_mui.c` nach dem Laden der
Konfiguration `localfs_set_tz(cfg.gmt_offset, cfg.dst);` aufrufen, wie
es `amicloud-shell` tut. Danach auf dem Amiga mit gesetztem
`gmt_offset` prüfen: Ein zweiter Abgleich direkt nach dem ersten darf
nichts mehr übertragen.
