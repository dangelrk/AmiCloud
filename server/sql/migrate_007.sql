-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Ronny Krueger
-- Migration 007: Amiga-Modus fuer Dateinamen.
-- Klassisches FFS/OFS speichert hoechstens 30 Zeichen und kuerzt laengere
-- Namen stillschweigend. Neue Konten legen deshalb keine Namen ueber 30
-- Zeichen mehr an (users.long_names = 0); in den persoenlichen
-- Einstellungen laesst sich das aufheben. Bestehende Namen bleiben
-- unangetastet, die Grenze gilt nur fuer neu angelegte.
--   * Konten, die schon Namen ueber 30 Zeichen haben, bekommen
--     long_names = 1 -- sonst blieben z. B. ihre Desktop-Abgleiche haengen.
--   * Alle anderen Konten: Amiga-Modus.
-- Kein Datenumbau. Einspielen mit: mysql -u root -p amicloud < migrate_007.sql
-- Bricht ab, wenn die Spalte schon existiert -- vorher pruefen mit
--   SHOW COLUMNS FROM users LIKE 'long_names';
-- Ohne diese Migration verhaelt sich der Server wie bisher (keine Grenze).

ALTER TABLE users
    ADD COLUMN long_names TINYINT(1) NOT NULL DEFAULT 0;

UPDATE users u SET u.long_names = 1
 WHERE EXISTS (SELECT 1 FROM files f
                WHERE f.user_id = u.id AND f.deleted_at IS NULL
                  AND CHAR_LENGTH(CONVERT(SUBSTRING_INDEX(f.path, '/', -1) USING utf8mb4)) > 30);
