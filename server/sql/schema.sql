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
    global_revision BIGINT UNSIGNED  NOT NULL DEFAULT 0, -- monotoner Aenderungszaehler (migrate_003)
    display_name    VARCHAR(128)     NOT NULL DEFAULT '',  -- ab hier migrate_004
    email           VARCHAR(255)     NOT NULL DEFAULT '',
    is_admin        TINYINT(1)       NOT NULL DEFAULT 0,
    quota_bytes     BIGINT UNSIGNED  NULL DEFAULT NULL,    -- NULL = unbegrenzt
    last_login      DATETIME         NULL DEFAULT NULL,
    session_epoch   INT UNSIGNED     NOT NULL DEFAULT 0    -- ++ macht Web-Sessions ungueltig
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS files (
    id          BIGINT UNSIGNED  AUTO_INCREMENT PRIMARY KEY,
    user_id     INT UNSIGNED     NOT NULL,
    path        VARBINARY(1024)  NOT NULL,             -- binaer wie das Dateisystem (migrate_005)
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

    UNIQUE KEY uq_user_path (user_id, path),
    KEY idx_user_changeseq (user_id, change_seq),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Benutzerverwaltung, Freigaben, Login-Bremse (migrate_004)
CREATE TABLE IF NOT EXISTS app_passwords (
    id           INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id      INT UNSIGNED NOT NULL,
    name         VARCHAR(64)  NOT NULL,
    token_hash   CHAR(64)     NOT NULL UNIQUE,  -- SHA-256 des Tokens
    created_at   DATETIME     DEFAULT CURRENT_TIMESTAMP,
    last_used_at DATETIME     NULL DEFAULT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS shares (
    id         INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id    INT UNSIGNED  NOT NULL,
    path       VARBINARY(1024) NOT NULL,        -- wie files.path (ohne fuehrenden /, binaer)
    token      CHAR(32)      NOT NULL UNIQUE,
    pass_hash  VARCHAR(255)  NULL DEFAULT NULL, -- optionales Link-Passwort
    expires_at DATE          NULL DEFAULT NULL,
    created_at DATETIME      DEFAULT CURRENT_TIMESTAMP,
    downloads  INT UNSIGNED  NOT NULL DEFAULT 0,
    KEY idx_user_path (user_id, path(255)),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS auth_failures (
    id       BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    ip       VARCHAR(45)  NOT NULL,
    scope    VARCHAR(96)  NOT NULL,  -- Benutzername bzw. "share:<token>"
    at       DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    KEY idx_ip_at (ip, at),
    KEY idx_ip_scope_at (ip, scope, at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Ersten Admin anlegen (nicht per SQL):
--   sudo -u apache php bin/amicloud-admin.php user:add --admin <name>

