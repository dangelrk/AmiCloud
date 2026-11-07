# ToDo

Derzeit keine offenen Punkte.

## Erledigt

- **Zeitzone in `amicloud-sync` und im MUI-Commander (Amiga)**, bekannt seit
  27.09.2026: Beide rufen nach dem Laden der Konfiguration
  `localfs_set_tz(cfg.gmt_offset, cfg.dst)` auf, wie `amicloud-shell`.
  `amicloud-sync` seit 0.2, der Commander seit 0.9.
