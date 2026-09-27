-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Ronny Krueger
-- Migration 003: Revisionen, globale Revision und Soft-Delete fuer den
-- metadatenbasierten Sync (siehe docs/api/API.md, Action "changes").
-- Einspielen mit: mysql -u root -p amicloud < migrate_003.sql
-- Idempotenz: bricht ab, wenn die Spalten schon existieren — vorher pruefen mit
--   SHOW COLUMNS FROM files LIKE 'revision';
--   SHOW COLUMNS FROM users LIKE 'global_revision';

ALTER TABLE users
    ADD COLUMN global_revision BIGINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT 'Monotoner Aenderungszaehler dieses Users (ueber alle Dateien)';

ALTER TABLE files
    ADD COLUMN revision   INT UNSIGNED NOT NULL DEFAULT 1
        COMMENT 'Inhalts-Revision: 1 bei Neuanlage, ++ nur bei Inhaltsaenderung',
    ADD COLUMN change_seq BIGINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT 'users.global_revision zum Zeitpunkt der letzten Aenderung dieser Zeile',
    ADD COLUMN deleted_at DATETIME NULL DEFAULT NULL
        COMMENT 'Soft-Delete-Tombstone; NULL = Datei lebt',
    ADD KEY idx_user_changeseq (user_id, change_seq);
