-- SPDX-License-Identifier: GPL-3.0-or-later
-- Copyright (C) 2026 Ronny Krueger
-- Migration 005: Pfade binaer vergleichen wie das Dateisystem.
-- Vorher verglich die Datenbank Pfade ohne Gross/Klein, Akzente und mit
-- PAD SPACE ("a.bin" = "A.bin" = "a.bin "), das Dateisystem aber exakt:
-- darueber liess sich das Speicherkontingent umgehen und eine Freigabe
-- auf einen anderen Ordner umbiegen. Der Unique-Index umfasst jetzt den
-- ganzen Pfad (vorher nur die ersten 768 Zeichen); sanitizePath()
-- begrenzt Pfade auf 1000 Byte.
-- Einspielen mit: mysql -u root -p amicloud < migrate_005.sql
-- Schlaegt fehl, wenn Pfade existieren, die sich nur in Gross/Klein
-- unterscheiden UND laenger als 1024 Byte sind -- vorher pruefen mit
--   SELECT COUNT(*) FROM files WHERE LENGTH(path) > 1024;

ALTER TABLE files
    DROP INDEX uq_user_path,
    MODIFY path VARBINARY(1024) NOT NULL,
    ADD UNIQUE KEY uq_user_path (user_id, path);

ALTER TABLE shares
    MODIFY path VARBINARY(1024) NOT NULL;
