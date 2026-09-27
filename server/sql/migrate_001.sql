-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Ronny Krueger
-- Migration 001: Amiga-Metadaten (protection, comment) + setdate-Unterstuetzung.
-- Einspielen mit: mysql -u root -p amicloud < migrate_001.sql
-- Idempotenz: bricht ab, wenn die Spalten schon existieren — vorher pruefen mit
--   SHOW COLUMNS FROM files LIKE 'protection';

ALTER TABLE files
    ADD COLUMN protection INT UNSIGNED NOT NULL DEFAULT 0  COMMENT 'Amiga-Protection-Bits (roh)',
    ADD COLUMN comment    VARCHAR(79)  NOT NULL DEFAULT '' COMMENT 'Amiga-Filecomment',
    -- ON UPDATE CURRENT_TIMESTAMP entfernen: modified_at wird explizit gesetzt
    -- (upload/setdate); Metadaten-Updates duerfen es nicht ueberschreiben.
    MODIFY modified_at DATETIME DEFAULT CURRENT_TIMESTAMP;
