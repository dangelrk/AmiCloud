-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Ronny Krueger
-- Migration 004: Benutzerverwaltung, App-Passwoerter, Quota, oeffentliche
-- Links, Login-Bremse (docs/design/user-management-plan.md).
-- Einspielen mit: mysql -u root -p amicloud < migrate_004.sql
-- Idempotenz: bricht ab, wenn die Spalten schon existieren — vorher pruefen mit
--   SHOW COLUMNS FROM users LIKE 'is_admin';
-- Danach den ersten Admin anlegen bzw. befoerdern:
--   sudo -u apache php bin/amicloud-admin.php user:admin <name> on

ALTER TABLE users
    ADD COLUMN display_name  VARCHAR(128)    NOT NULL DEFAULT '',
    ADD COLUMN email         VARCHAR(255)    NOT NULL DEFAULT '',
    ADD COLUMN is_admin      TINYINT(1)      NOT NULL DEFAULT 0,
    ADD COLUMN quota_bytes   BIGINT UNSIGNED NULL DEFAULT NULL
        COMMENT 'Speicherkontingent; NULL = unbegrenzt',
    ADD COLUMN last_login    DATETIME        NULL DEFAULT NULL,
    ADD COLUMN session_epoch INT UNSIGNED    NOT NULL DEFAULT 0
        COMMENT 'Erhoeht bei Passwortwechsel/Deaktivierung -> Web-Sessions ungueltig';

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
    path       VARCHAR(1024) NOT NULL,          -- wie files.path (ohne fuehrenden /)
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
