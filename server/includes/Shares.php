<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
if (!defined('AMICLOUD')) { http_response_code(403); exit; }

/* Oeffentliche Links auf eine Datei oder einen Ordner (share.php). Pfade
 * wie files.path: ohne fuehrenden "/". Umbenennen/Loeschen ueber API und
 * Web zieht die Links mit (FileStore::renameImpl/deleteImpl). */
class Shares
{
    public static function create(int $uid, string $path, string $pass, string $expires): array
    {
        $dbPath = dbPath($path);
        if ($dbPath === '') {
            return ['', 'Der Stammordner kann nicht geteilt werden.'];
        }
        if ($pass !== '' && strlen($pass) > 200) {
            return ['', 'Das Link-Passwort ist zu lang.'];
        }
        $exp = null;
        if ($expires !== '') {
            $d = DateTime::createFromFormat('Y-m-d', $expires);
            if ($d === false || $d->format('Y-m-d') !== $expires) {
                return ['', 'Ungültiges Ablaufdatum.'];
            }
            if ($expires < date('Y-m-d')) {
                return ['', 'Das Ablaufdatum liegt in der Vergangenheit.'];
            }
            $exp = $expires;
        }
        $token = bin2hex(random_bytes(16));
        DB::get()->prepare(
            'INSERT INTO shares (user_id, path, token, pass_hash, expires_at) VALUES (?, ?, ?, ?, ?)'
        )->execute([
            $uid, $dbPath, $token,
            $pass === '' ? null : password_hash($pass, PASSWORD_BCRYPT, ['cost' => Users::BCRYPT_COST]),
            $exp,
        ]);
        return [$token, null];
    }

    public static function listFor(int $uid): array
    {
        $stmt = DB::get()->prepare(
            'SELECT id, path, token, pass_hash IS NOT NULL AS has_pass, expires_at, created_at, downloads
               FROM shares WHERE user_id = ? ORDER BY path, created_at'
        );
        $stmt->execute([$uid]);
        return $stmt->fetchAll();
    }

    public static function forPath(int $uid, string $path): array
    {
        $stmt = DB::get()->prepare(
            'SELECT id, path, token, pass_hash IS NOT NULL AS has_pass, expires_at, created_at, downloads
               FROM shares WHERE user_id = ? AND path = ? ORDER BY created_at'
        );
        $stmt->execute([$uid, dbPath($path)]);
        return $stmt->fetchAll();
    }

    /* Pfade (mit fuehrendem "/") aller geteilten Eintraege -- fuer das
     * Freigabe-Symbol in der Dateiliste */
    public static function sharedPaths(int $uid): array
    {
        $stmt = DB::get()->prepare('SELECT DISTINCT path FROM shares WHERE user_id = ?');
        $stmt->execute([$uid]);
        $out = [];
        foreach ($stmt->fetchAll() as $r) {
            $out['/' . $r['path']] = true;
        }
        return $out;
    }

    /* Gueltiger Link inkl. Eigentuemer; null bei unbekannt, abgelaufen
     * oder deaktiviertem Eigentuemer -- fuer den Besucher alles "404". */
    public static function byToken(string $token): ?array
    {
        if (!preg_match('/^[0-9a-f]{32}$/', $token)) {
            return null;
        }
        $stmt = DB::get()->prepare(
            'SELECT s.id, s.user_id, s.path, s.token, s.pass_hash, s.expires_at,
                    u.username, u.display_name, u.active
               FROM shares s JOIN users u ON u.id = s.user_id WHERE s.token = ?'
        );
        $stmt->execute([$token]);
        $row = $stmt->fetch();
        if (!$row || !(int)$row['active']) {
            return null;
        }
        if ($row['expires_at'] !== null && $row['expires_at'] < date('Y-m-d')) {
            return null;
        }
        return $row;
    }

    public static function delete(int $uid, int $id): void
    {
        DB::get()->prepare('DELETE FROM shares WHERE id = ? AND user_id = ?')->execute([$id, $uid]);
    }

    public static function countDownload(int $id): void
    {
        DB::get()->prepare('UPDATE shares SET downloads = downloads + 1 WHERE id = ?')->execute([$id]);
    }

    /* ── Mitziehen bei Umbenennen/Loeschen (exakter Pfad + Praefix) ──── */

    public static function onRename(int $uid, string $fromDb, string $toDb): void
    {
        $db = DB::get();
        $db->prepare('UPDATE shares SET path = ? WHERE user_id = ? AND path = ?')
           ->execute([$toDb, $uid, $fromDb]);
        $db->prepare(
            'UPDATE shares SET path = CONCAT(?, SUBSTRING(path, ?)) WHERE user_id = ? AND path LIKE ?'
        )->execute([$toDb . '/', strlen($fromDb) + 2, $uid, FileStore::likeEscape($fromDb) . '/%']);
    }

    public static function onDelete(int $uid, string $dbPath): void
    {
        $db = DB::get();
        $db->prepare('DELETE FROM shares WHERE user_id = ? AND (path = ? OR path LIKE ?)')
           ->execute([$uid, $dbPath, FileStore::likeEscape($dbPath) . '/%']);
    }
}
