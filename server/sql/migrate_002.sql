-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Ronny Krueger
-- Migration 002: MD5-Pruefsummen fuer Uebertragungscheck & sync (v4, Abschnitt 18).
-- Einspielen mit: mysql -u root -p amicloud < migrate_002.sql
-- Idempotenz: bricht ab, wenn die Spalte schon existiert — vorher pruefen mit
--   SHOW COLUMNS FROM files LIKE 'checksum';

ALTER TABLE files
    ADD COLUMN checksum CHAR(32) NULL DEFAULT NULL COMMENT 'MD5-Hex (32 Zeichen), NULL = noch nicht berechnet';
