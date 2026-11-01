-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Ronny Krueger
-- Migration 006: action=changes fuer den schrittweisen Abgleich
-- (docs/design/desktop-sync-plan.md, Phase 6).
--   * users.tombstone_seq: hoechste change_seq aufgeraeumter Grabsteine.
--     Wer changes mit einem aelteren since fragt, bekommt 410 und gleicht
--     voll ab (Grabsteine werden nach 30 Tagen entfernt).
--   * Zeilen von vor migrate_003 hatten change_seq = 0 und fehlten in
--     changes?since=0 ganz; sie bekommen 1 (= "vor langer Zeit").
-- Kein Datenumbau. Einspielen mit: mysql -u root -p amicloud < migrate_006.sql
-- Bricht ab, wenn die Spalte schon existiert -- vorher pruefen mit
--   SHOW COLUMNS FROM users LIKE 'tombstone_seq';

ALTER TABLE users
    ADD COLUMN tombstone_seq BIGINT UNSIGNED NOT NULL DEFAULT 0;

UPDATE files SET change_seq = 1 WHERE change_seq = 0;
UPDATE users SET global_revision = 1 WHERE global_revision = 0;
