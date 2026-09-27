-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Ronny Krueger
-- AmiCloud database schema
-- Run as: mysql -u root -p < schema.sql

CREATE DATABASE IF NOT EXISTS amicloud
    CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE amicloud;

CREATE TABLE IF NOT EXISTS users (
    id              INT UNSIGNED     AUTO_INCREMENT PRIMARY KEY,
    username        VARCHAR(64)      NOT NULL UNIQUE,
    pass_hash       VARCHAR(255)     NOT NULL,
    created_at      DATETIME         DEFAULT CURRENT_TIMESTAMP,
    active          TINYINT(1)       DEFAULT 1,
    global_revision BIGINT UNSIGNED  NOT NULL DEFAULT 0  -- monotoner Aenderungszaehler (migrate_003)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS files (
    id          BIGINT UNSIGNED  AUTO_INCREMENT PRIMARY KEY,
    user_id     INT UNSIGNED     NOT NULL,
    path        VARCHAR(1024)    NOT NULL,
    size_bytes  BIGINT UNSIGNED  DEFAULT 0,
    mime_type   VARCHAR(128),
    created_at  DATETIME         DEFAULT CURRENT_TIMESTAMP,
    -- Kein ON UPDATE CURRENT_TIMESTAMP: modified_at wird explizit gesetzt
    -- (upload) bzw. per action=setdate — Metadaten-Updates duerfen es nicht
    -- ueberschreiben (Amiga SetDate/COPY CLONE).
    modified_at DATETIME         DEFAULT CURRENT_TIMESTAMP,
    is_dir      TINYINT(1)       DEFAULT 0,
    protection  INT UNSIGNED     NOT NULL DEFAULT 0,   -- Amiga-Protection-Bits (roh)
    comment     VARCHAR(79)      NOT NULL DEFAULT '',  -- Amiga-Filecomment
    checksum    CHAR(32)         NULL DEFAULT NULL,    -- MD5-Hex, NULL = noch nicht berechnet
    revision    INT UNSIGNED     NOT NULL DEFAULT 1,   -- Inhalts-Revision (migrate_003)
    change_seq  BIGINT UNSIGNED  NOT NULL DEFAULT 0,   -- global_revision beim letzten Change (migrate_003)
    deleted_at  DATETIME         NULL DEFAULT NULL,    -- Soft-Delete-Tombstone (migrate_003)

    UNIQUE KEY uq_user_path (user_id, path(768)),
    KEY idx_user_changeseq (user_id, change_seq),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Add first user (password must be set via PHP: password_hash('pass', PASSWORD_BCRYPT))
-- Example insert (replace hash with real one):
-- INSERT INTO users (username, pass_hash) VALUES ('admin', '$2y$12$...');
