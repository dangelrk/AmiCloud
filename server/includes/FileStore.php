<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
if (!defined('AMICLOUD')) { http_response_code(403); exit; }

/* Alle Zeitstempel (list/info/status/setdate) in derselben Zeitzone wie
 * der Amiga-Client (immer lokale Systemzeit) — bewusst NICHT UTC, siehe
 * config.php.example. Einmal pro Request gesetzt (FileStore.php wird von
 * api.php UND web.php eingebunden). */
date_default_timezone_set(defined('AMICLOUD_TIMEZONE') ? AMICLOUD_TIMEZONE : 'Europe/Berlin');

/* Returns a normalised, safe server-relative path like /subdir/file.txt */
function sanitizePath(string $raw): string
{
    $path = str_replace('\\', '/', $raw);
    $parts = explode('/', $path);
    $safe  = [];
    foreach ($parts as $part) {
        if ($part === '' || $part === '.') {
            continue;
        }
        if ($part === '..') {
            Response::error(400, 'Path traversal not allowed');
        }
        if (preg_match('/[\x00<>:"|?*]/', $part)) {
            Response::error(400, 'Illegal character in path');
        }
        /* files.path vergleicht binaer (migrate_005): ungueltiges UTF-8
         * und ueberlange Namen wuerden sonst erst beim INSERT scheitern --
         * nachdem die Datei schon auf der Platte liegt. */
        if (!mb_check_encoding($part, 'UTF-8')) {
            Response::error(400, 'Path is not valid UTF-8');
        }
        if (strlen($part) > 255) {
            Response::error(400, 'Path segment too long');
        }
        if (strncmp($part, UPLOAD_TMP_PREFIX, strlen(UPLOAD_TMP_PREFIX)) === 0) {
            Response::error(400, 'Reserved name');
        }
        $safe[] = $part;
    }
    $out = '/' . implode('/', $safe);
    if (strlen($out) > 1000) {
        Response::error(400, 'Path too long');
    }
    return $out;
}

/* Namensvorsatz der Temp-Dateien laufender Uploads (uploadImpl());
 * Listings blenden sie aus, Clients koennen sie nicht anlegen. */
const UPLOAD_TMP_PREFIX = '.amicloud-upload-';

/* Absolute filesystem path for a user-relative path */
function storagePath(string $relpath = ''): string
{
    $base = rtrim(STORAGE_PATH, '/') . '/' . Auth::$username;
    if ($relpath !== '' && $relpath !== '/') {
        $base .= '/' . ltrim($relpath, '/');
    }
    return $base;
}

function dbPath(string $relpath): string
{
    return ltrim($relpath, '/');
}

/* Einheitliches Zeitformat "YYYY-MM-DD HH:MM:SS" fuer alle API-Antworten
 * (list/info/status), in der oben gesetzten lokalen Zeitzone — dieselbe
 * Darstellung wie bei den lokalen Zeitstempeln von lls/ldir/llist. */
function localDateStr(int $ts): string
{
    return date('Y-m-d H:i:s', $ts);
}

class FileStore
{
    /* ══ Metadaten-Helfer (protection/comment liegen nur in der DB) ═════ */

    /* Escape %, _ und \ fuer LIKE-Muster (auch von Shares benutzt) */
    public static function likeEscape(string $s): string
    {
        return str_replace(['\\', '%', '_'], ['\\\\', '\\%', '\\_'], $s);
    }

    /* Erhoeht Auth::$user_id's globalen Aenderungszaehler atomar und liefert
     * den neuen Wert — jeder schreibende Vorgang (upload/delete/rename)
     * ruft dies genau einmal pro API-Aufruf und schreibt das Ergebnis in
     * change_seq der betroffenen Zeile(n) (Abschnitt "Globale Server-
     * Revision", Sync-Metadaten, docs/api/API.md "changes"). setdate/
     * setcomment/setprotect bumpen bewusst NICHT — mtime ist reine Info. */
    private static function bumpGlobalRevision(): int
    {
        DB::get()->prepare(
            'UPDATE users SET global_revision = LAST_INSERT_ID(global_revision + 1) WHERE id = ?'
        )->execute([Auth::$user_id]);
        return (int)DB::get()->lastInsertId();
    }

    /* Aktueller Stand von Auth::$user_id's globalem Zaehler, ohne ihn zu
     * erhoehen — fuer action=status (billiger "nichts geaendert seit N"-
     * Kurzschluss) und die global_revision im action=changes-Envelope. */
    public static function currentGlobalRevision(): int
    {
        $stmt = DB::get()->prepare('SELECT global_revision FROM users WHERE id = ?');
        $stmt->execute([Auth::$user_id]);
        return (int)($stmt->fetchColumn() ?: 0);
    }

    /* path => ['id'=>int, 'protection'=>int, 'comment'=>string,
     *          'checksum'=>?string, 'size_bytes'=>int, 'mtime_ts'=>?int,
     *          'revision'=>int, 'change_seq'=>int] fuer eine Pfadliste.
     * mtime_ts ist UNIX_TIMESTAMP(modified_at) — Vergleichsbasis fuer den
     * MD5-Lazy-Backfill in md5For() (Abschnitt 18.2). deleted_at IS NULL:
     * eine soft-geloeschte Zeile darf nie die Metadaten eines gleichnamigen,
     * aus dem API-Fluss heraus neu erschienenen Eintrags verunreinigen —
     * kommt ueber die API selbst nicht vor (upload() loescht deleted_at),
     * ist aber eine billige Absicherung. */
    private static function metaMap(array $dbPaths): array
    {
        if ($dbPaths === []) {
            return [];
        }
        $ph   = implode(',', array_fill(0, count($dbPaths), '?'));
        $stmt = DB::get()->prepare(
            "SELECT id, path, protection, comment, checksum, size_bytes, revision, change_seq,
                    UNIX_TIMESTAMP(modified_at) AS mtime_ts
             FROM files WHERE user_id = ? AND path IN ($ph) AND deleted_at IS NULL"
        );
        $stmt->execute(array_merge([Auth::$user_id], $dbPaths));
        $map = [];
        foreach ($stmt->fetchAll() as $row) {
            $map[$row['path']] = $row;
        }
        return $map;
    }

    /* MD5 einer Datei aus der DB (falls frisch) oder frisch berechnet und
     * gespeichert ("Lazy Backfill mit Frische-Check", Abschnitt 18.2).
     * "Frisch" heisst: Zeile vorhanden, checksum nicht NULL, und
     * size_bytes/modified_at stimmen noch mit dem Dateisystem ueberein.
     * Bei jedem Neuberechnen werden size_bytes/modified_at gleich mit mit-
     * geheilt, falls sie ausserhalb der API veraendert wurden. Nur fuer
     * Dateien gedacht — Aufrufer filtert Verzeichnisse vorher aus. */
    private static function md5For(string $absPath, string $dbPath, array $map): string
    {
        if (!is_file($absPath)) {
            return '';
        }
        $row      = $map[$dbPath] ?? null;
        $curSize  = filesize($absPath);
        $curMtime = filemtime($absPath);
        $fresh    = $row !== null
            && $row['checksum'] !== null
            && (int)$row['size_bytes'] === $curSize
            && $row['mtime_ts'] !== null
            && (int)$row['mtime_ts'] === $curMtime;

        if ($fresh) {
            return (string)$row['checksum'];
        }

        $sum = md5_file($absPath);
        DB::get()->prepare(
            'INSERT INTO files (user_id, path, size_bytes, is_dir, checksum, modified_at)
             VALUES (?, ?, ?, 0, ?, FROM_UNIXTIME(?))
             ON DUPLICATE KEY UPDATE
               checksum = VALUES(checksum),
               size_bytes = VALUES(size_bytes),
               modified_at = VALUES(modified_at)'
        )->execute([Auth::$user_id, $dbPath, $curSize, $sum, $curMtime]);

        return $sum;
    }

    /* Eintrag um protection/comment/id/revision/change_seq aus der Meta-Map
     * ergaenzen. id/revision/change_seq bleiben 0, wenn der Pfad (noch)
     * keine files-Zeile hat (z.B. implizit angelegtes Verzeichnis) — ein
     * Client, der diese Felder nicht kennt oder eine 0 vorfindet, faellt
     * auf reinen MD5-Vergleich zurueck (Sync-Metadaten, docs/api/API.md "changes"). */
    private static function withMeta(array $entry, array $map, string $dbPath): array
    {
        $m = $map[$dbPath] ?? null;
        $entry['protection']  = $m ? (int)$m['protection']  : 0;
        $entry['comment']     = $m ? (string)$m['comment']  : '';
        $entry['id']          = $m ? (int)$m['id']          : 0;
        $entry['revision']    = $m ? (int)$m['revision']    : 0;
        $entry['change_seq']  = $m ? (int)$m['change_seq']  : 0;
        return $entry;
    }

    /*
     * Eine Metadaten-Spalte setzen. Legt die DB-Zeile bei Bedarf an
     * (Verzeichnisse, die nur implizit als Upload-Parent entstanden sind,
     * haben keine Zeile). $column ist eine interne Konstante, nie User-Input.
     * Rueckgabe: [HTTP-Code, Meldung]; Code 0 = Erfolg.
     */
    private static function metaImpl(string $path, string $column, $value): array
    {
        $target = storagePath($path);
        if ($path === '/' || !file_exists($target)) {
            return [404, 'Not found'];
        }
        $isDir = is_dir($target) ? 1 : 0;
        $size  = $isDir ? 0 : (int)filesize($target);

        DB::get()->prepare(
            "INSERT INTO files (user_id, path, size_bytes, is_dir, $column)
             VALUES (?, ?, ?, ?, ?)
             ON DUPLICATE KEY UPDATE $column = VALUES($column)"
        )->execute([Auth::$user_id, dbPath($path), $size, $isDir, $value]);

        return [0, ''];
    }

    /* ------------------------------------------------------------------ */
    public static function list(string $path): void
    {
        $dir = storagePath($path);

        /* Auto-create user root on first access */
        if ($path === '/' && !is_dir($dir)) {
            mkdir($dir, 0755, true);
        }

        if (!is_dir($dir)) {
            Response::error(404, 'Directory not found');
        }

        Response::ok(['path' => $path, 'entries' => self::scanEntries($dir, $path)]);
    }

    /* Verzeichnis scannen und Eintraege inkl. DB-Metadaten liefern */
    private static function scanEntries(string $dir, string $path): array
    {
        $prefix  = rtrim(dbPath($path), '/');
        $prefix  = $prefix === '' ? '' : $prefix . '/';
        $items   = [];
        $dbPaths = [];
        foreach (scandir($dir) as $item) {
            if ($item === '.' || $item === '..'
                || strncmp($item, UPLOAD_TMP_PREFIX, strlen(UPLOAD_TMP_PREFIX)) === 0) {
                continue;
            }
            $items[]   = $item;
            $dbPaths[] = $prefix . $item;
        }
        $map = self::metaMap($dbPaths);

        $entries = [];
        foreach ($items as $i => $item) {
            $fp     = $dir . '/' . $item;
            $isDir  = is_dir($fp);
            $mtime  = (int)filemtime($fp);
            $entries[] = self::withMeta([
                'name'     => $item,
                'type'     => $isDir ? 'dir' : 'file',
                'size'     => $isDir ? 0 : (int)filesize($fp),
                'modified' => localDateStr($mtime),
                'mtime'    => $mtime,
                'md5'      => $isDir ? '' : self::md5For($fp, $dbPaths[$i], $map),
            ], $map, $dbPaths[$i]);
        }
        usort($entries, fn($a, $b) => strcmp($a['name'], $b['name']));
        return $entries;
    }

    /* ------------------------------------------------------------------ */
    public static function download(string $path, int $offset, int $length): void
    {
        $file = storagePath($path);

        if (!is_file($file)) {
            Response::error(404, 'File not found');
        }

        /* Immer als Download und sandboxed: hochgeladenes HTML/SVG darf
         * unter der eigenen Origin nie gerendert werden (es kaeme an die
         * Web-Session heran). Die Clients werten den Typ nicht aus. */
        $mime = 'application/octet-stream';
        header('X-Content-Type-Options: nosniff');
        header("Content-Security-Policy: sandbox; default-src 'none'");
        header('Content-Disposition: attachment; filename="'
               . preg_replace('/[^\x20-\x7e]|["\\\\]/', '_', basename($file)) . '"');

        /* Range: bytes=a- / bytes=a-b (RFC 9110 14.2): damit setzt der
         * Client einen abgebrochenen Download fort, statt bei einer
         * Modemverbindung wieder bei null anzufangen. Nur ein einzelner
         * Bereich; alles andere wird ignoriert (volle Antwort, 200). */
        $partial = false;
        if ($offset === 0 && $length === -1
            && preg_match('/^bytes=(\d+)-(\d*)$/', trim($_SERVER['HTTP_RANGE'] ?? ''), $m)) {
            $size  = (int)filesize($file);
            $start = (int)$m[1];
            if ($start >= $size) {
                header('Content-Range: bytes */' . $size);
                Response::error(416, 'Range not satisfiable');
            }
            $offset  = $start;
            $length  = $m[2] !== '' ? max(0, (int)$m[2] - $start + 1) : -1;
            $partial = true;
        }

        /* X-AmiCloud-MD5 gilt immer fuer die Gesamtdatei — bei Teil-
         * Downloads (offset/length bzw. Range) wird der Header weggelassen
         * statt eine irrefuehrende Teil-Pruefsumme zu senden (Abschnitt
         * 18.2). offset===0 && length===-1 ist der von api.php verwendete
         * Sentinel fuer "kein offset/length angegeben". */
        if ($offset === 0 && $length === -1) {
            $dbPath = dbPath($path);
            $sum    = self::md5For($file, $dbPath, self::metaMap([$dbPath]));
            if ($sum !== '') {
                header('X-AmiCloud-MD5: ' . $sum);
            }
        }

        Response::stream($file, $mime, $offset, $length, $partial);
    }

    /* ------------------------------------------------------------------ */
    public static function upload(string $path, string $data, ?string $expectedMd5 = null,
                                  ?int $ifRevision = null, bool $createOnly = false,
                                  ?int $ifId = null): void
    {
        $r = self::uploadImpl($path, $data, null, $expectedMd5, $ifRevision, $createOnly, $ifId);
        if ($r[0] !== 0) {
            Response::error($r[0], $r[1]);
        }
        Response::ok($r[2]);
    }

    /* Gemeinsamer Schreibweg fuer API (Body als String, $data) und Web
     * (hochgeladene Temp-Datei, $tmpFile): Kontingent, Revision,
     * globale Revision, Tombstone wiederbeleben. Kontingentpruefung und
     * Schreiben laufen in einer Transaktion mit der users-Zeile gesperrt
     * (FOR UPDATE), damit parallele Uploads das Kontingent nicht gemeinsam
     * ueberschreiten. Rueckgabe [0, '', Antwortdaten] oder [Code, Meldung].
     *
     * Bedingungen fuer den Abgleich (412 Precondition Failed, nichts wird
     * geschrieben): $ifRevision/$ifId -- die Datei muss existieren und
     * genau diese Revision bzw. ID haben (eine Umbenennung auf diesen Pfad
     * behaelt die Revision, wechselt aber die ID); $createOnly -- die Datei darf noch nicht
     * existieren. Geprueft innerhalb der Transaktion, also atomar gegen
     * andere Uploads desselben Benutzers. */
    private static function uploadImpl(string $path, ?string $data, ?string $tmpFile, ?string $expectedMd5,
                                       ?int $ifRevision = null, bool $createOnly = false,
                                       ?int $ifId = null): array
    {
        $ifRevision = ($ifRevision ?? 0) > 0 ? $ifRevision : null;
        $ifId       = ($ifId ?? 0) > 0 ? $ifId : null;
        if ($path === '/') {
            return [400, 'Cannot upload to root'];
        }
        /* Leerer Body ist erlaubt: 0-Byte-Datei (Amiga-Handler legt bei
         * Open(MODE_NEWFILE) + Close ohne Write eine leere Datei an). */
        $size = $data !== null ? strlen($data) : (int)filesize((string)$tmpFile);
        if ($size > MAX_UPLOAD) {
            return [413, 'Upload exceeds maximum allowed size'];
        }

        $sum = $data !== null ? md5($data) : (string)md5_file((string)$tmpFile);
        if ($expectedMd5 !== null && $expectedMd5 !== ''
            && !hash_equals(strtolower($sum), strtolower($expectedMd5))) {
            return [422, "MD5 mismatch: expected $expectedMd5, got $sum"];
        }

        $file = storagePath($path);
        if (is_dir($file)) {
            return [409, 'A directory with this name exists'];
        }
        $dir = dirname($file);
        if (!is_dir($dir) && !mkdir($dir, 0755, true)) {
            return [500, 'Cannot create parent directory'];
        }

        $dbP = dbPath($path);
        $db  = DB::get();
        $db->beginTransaction();
        try {
            $q = $db->prepare('SELECT quota_bytes FROM users WHERE id = ? FOR UPDATE');
            $q->execute([Auth::$user_id]);
            $quota = $q->fetchColumn();

            /* Revision: 1 bei Neuanlage, ++ nur bei tatsaechlicher Inhalts-
             * aenderung (auch beim Wiederbeleben eines zuvor geloeschten
             * Pfades) — Umbenennen/Verschieben laesst sie unberuehrt (siehe
             * renameImpl()). Vorherige Zeile wird unabhaengig von deleted_at
             * gesucht, damit ein Re-Upload denselben Verlauf fortsetzt. */
            $prev = $db->prepare('SELECT id, checksum, revision, size_bytes, deleted_at FROM files WHERE user_id = ? AND path = ?');
            $prev->execute([Auth::$user_id, $dbP]);
            $existing = $prev->fetch();

            $live = is_file($file);
            if ($createOnly && $live) {
                $db->rollBack();
                return [412, 'File exists'];
            }
            if (($ifRevision !== null || $ifId !== null)
                && (!$live || $existing === false || $existing['deleted_at'] !== null
                    || ($ifRevision !== null && (int)$existing['revision'] !== $ifRevision)
                    || ($ifId !== null && (int)$existing['id'] !== $ifId))) {
                $db->rollBack();
                return [412, 'Revision mismatch'];
            }

            if ($quota !== null && $quota !== false) {
                $oldSize = ($existing && $existing['deleted_at'] === null) ? (int)$existing['size_bytes'] : 0;
                if (Users::usedBytes(Auth::$user_id) - $oldSize + $size > (int)$quota) {
                    $db->rollBack();
                    return [507, 'Quota exceeded'];
                }
            }

            /* Erst in eine Temp-Datei daneben schreiben und erst nach dem
             * erfolgreichen INSERT ans Ziel umbenennen: scheitert der
             * DB-Teil, bleibt keine ungezaehlte Datei liegen. */
            $stage   = $dir . '/' . UPLOAD_TMP_PREFIX . bin2hex(random_bytes(8));
            $written = $data !== null
                ? file_put_contents($stage, $data) !== false
                : (is_uploaded_file((string)$tmpFile)
                    ? move_uploaded_file((string)$tmpFile, $stage)
                    : rename((string)$tmpFile, $stage));
            if (!$written) {
                @unlink($stage);
                $db->rollBack();
                return [500, 'Write failed'];
            }

            $mime     = mime_content_type($stage) ?: 'application/octet-stream';
            $revision = ($existing === false)
                ? 1
                : (($existing['checksum'] === $sum) ? (int)$existing['revision'] : (int)$existing['revision'] + 1);
            $changeSeq = self::bumpGlobalRevision();

            $db->prepare(
                'INSERT INTO files (user_id, path, size_bytes, mime_type, is_dir, checksum, revision, change_seq, deleted_at)
                 VALUES (:uid, :p, :sz, :mime, 0, :sum, :rev, :cs, NULL)
                 ON DUPLICATE KEY UPDATE
                   size_bytes  = VALUES(size_bytes),
                   mime_type   = VALUES(mime_type),
                   modified_at = NOW(),
                   is_dir      = 0,
                   checksum    = VALUES(checksum),
                   revision    = VALUES(revision),
                   change_seq  = VALUES(change_seq),
                   deleted_at  = NULL'
            )->execute([
                ':uid'  => Auth::$user_id,
                ':p'    => $dbP,
                ':sz'   => $size,
                ':mime' => $mime,
                ':sum'  => $sum,
                ':rev'  => $revision,
                ':cs'   => $changeSeq,
            ]);
            if (!rename($stage, $file)) {
                throw new RuntimeException('rename to target failed');
            }
            $idq = $db->prepare('SELECT id FROM files WHERE user_id = ? AND path = ?');
            $idq->execute([Auth::$user_id, $dbP]);
            $fileId = (int)$idq->fetchColumn();
            $db->commit();
        } catch (Throwable $e) {
            if (isset($stage) && is_file($stage)) {
                @unlink($stage);
            }
            if ($db->inTransaction()) {
                $db->rollBack();
            }
            throw $e;
        }

        return [0, '', [
            'path'       => $path,
            'size'       => $size,
            'md5'        => $sum,
            'mtime'      => filemtime($file),
            'revision'   => $revision,
            'change_seq' => $changeSeq,
            'id'         => $fileId,
        ]];
    }

    /* ------------------------------------------------------------------ */
    public static function delete(string $path, bool $recursive = false,
                                  ?int $ifRevision = null, ?int $ifId = null): void
    {
        $ifRevision = ($ifRevision ?? 0) > 0 ? $ifRevision : null;
        $ifId       = ($ifId ?? 0) > 0 ? $ifId : null;
        if ($ifRevision !== null || $ifId !== null) {
            self::deleteIfUnchanged($path, $ifRevision, $ifId);
            return;
        }
        $result = self::deleteImpl($path, $recursive);
        if ($result !== true) {
            [$code, $message] = $result;
            Response::error($code, $message);
        }
        Response::ok(['path' => $path]);
    }

    /* Web-UI-Variante von delete() — Bool statt JSON-Response, damit web.php
     * per redirect+Banner reagieren kann statt eine API-Fehlerantwort zu
     * bekommen. $recursive default false aus Kompatibilitaetsgruenden fuer
     * evtl. andere Aufrufer; web.php uebergibt immer true (Abschnitt 3 des
     * Web-Delete-Plans: fuer Dateien macht das Flag ohnehin keinen
     * Unterschied, siehe deleteImpl()). */
    public static function deleteRaw(string $path, bool $recursive = false): bool
    {
        return self::deleteImpl($path, $recursive) === true;
    }

    /* Gemeinsamer Kern von delete()/deleteRaw(): entfernt den Dateisystem-
     * Eintrag unwiderruflich (rekursiv bei nicht-leeren Verzeichnissen,
     * wenn $recursive), markiert die zugehoerige(n) DB-Zeile(n) aber nur
     * als Tombstone (deleted_at, inkl. aller Kind-Pfade unter $path, wenn
     * $recursive) statt sie zu loeschen — Soft-Delete, damit action=changes
     * eine Loeschung als Delta-Eintrag ausliefern kann (docs/api/API.md,
     * "changes"). Ein Re-Upload desselben Pfades belebt die Zeile wieder (siehe
     * upload()). true bei Erfolg, sonst [HTTP-Code, Fehlertext] — die
     * Aufrufer entscheiden, wie sie das nach aussen zeigen. */
    /* Bedingtes Loeschen fuer den Abgleich: nur eine Datei, die noch die
     * angegebene Revision bzw. ID hat -- sonst 412 und nichts geloescht
     * (eine fremde Aenderung seit dem letzten list des Clients bleibt).
     * Unter derselben Sperre der users-Zeile wie uploadImpl(), also
     * atomar gegen Uploads desselben Benutzers. */
    private static function deleteIfUnchanged(string $path, ?int $ifRevision, ?int $ifId): void
    {
        $db = DB::get();
        $db->beginTransaction();
        try {
            $db->prepare('SELECT id FROM users WHERE id = ? FOR UPDATE')->execute([Auth::$user_id]);
            $q = $db->prepare('SELECT id, revision, deleted_at FROM files WHERE user_id = ? AND path = ?');
            $q->execute([Auth::$user_id, dbPath($path)]);
            $row = $q->fetch();
            if (!is_file(storagePath($path)) || $row === false || $row['deleted_at'] !== null
                || ($ifRevision !== null && (int)$row['revision'] !== $ifRevision)
                || ($ifId !== null && (int)$row['id'] !== $ifId)) {
                $db->rollBack();
                Response::error(412, 'Revision mismatch');
            }
            $result = self::deleteImpl($path, false);
            if ($result !== true) {
                $db->rollBack();
                Response::error($result[0], $result[1]);
            }
            $db->commit();
        } catch (Throwable $e) {
            if ($db->inTransaction()) {
                $db->rollBack();
            }
            throw $e;
        }
        Response::ok(['path' => $path]);
    }

    private static function deleteImpl(string $path, bool $recursive)
    {
        if ($path === '/') {
            return [400, 'Cannot delete root'];
        }
        $target = storagePath($path);

        if (is_dir($target)) {
            if ($recursive) {
                self::deleteTree($target);
            } elseif (!@rmdir($target)) {
                return [409, 'Directory not empty or cannot be removed'];
            }
        } elseif (is_file($target)) {
            if (!unlink($target)) {
                return [500, 'Cannot delete file'];
            }
        } else {
            return [404, 'Not found'];
        }

        $dbPath    = dbPath($path);
        $db        = DB::get();
        $changeSeq = self::bumpGlobalRevision();
        $db->prepare('UPDATE files SET deleted_at = NOW(), change_seq = ? WHERE user_id = ? AND path = ?')
           ->execute([$changeSeq, Auth::$user_id, $dbPath]);
        if ($recursive) {
            /* Kind-Zeilen aufraeumen — gleiches Praefix-Muster wie renameImpl() */
            $db->prepare('UPDATE files SET deleted_at = NOW(), change_seq = ? WHERE user_id = ? AND path LIKE ?')
               ->execute([$changeSeq, Auth::$user_id, self::likeEscape($dbPath) . '/%']);
        }
        Shares::onDelete(Auth::$user_id, $dbPath);

        return true;
    }

    /* Datei-/Ordner-Anzahl + Gesamtgroesse von $path selbst plus allem
     * darunter — fuer eine Vorschau vor dem (rekursiven) Loeschen im
     * Web-UI. Reine DB-Abfrage nach demselben Praefix-Muster wie die
     * Kind-Zeilen-Bereinigung in deleteImpl() (path = ? OR path LIKE
     * 'prefix/%'), kein Dateisystem-Walk noetig — anders als dirSize()
     * (nur Groesse, fuer info()) ist size_bytes in der DB fuer Dateien
     * bereits konsistent gehalten (siehe upload()/uploadRaw()). */
    public static function countAndSize(string $path): array
    {
        $dbPath = dbPath($path);
        $stmt = DB::get()->prepare(
            'SELECT
                COALESCE(SUM(is_dir = 0), 0) AS files,
                COALESCE(SUM(is_dir = 1), 0) AS dirs,
                COALESCE(SUM(CASE WHEN is_dir = 0 THEN size_bytes ELSE 0 END), 0) AS bytes
             FROM files
             WHERE user_id = ? AND (path = ? OR path LIKE ?) AND deleted_at IS NULL'
        );
        $stmt->execute([Auth::$user_id, $dbPath, self::likeEscape($dbPath) . '/%']);
        $row = $stmt->fetch();

        return [
            'files' => (int)($row['files'] ?? 0),
            'dirs'  => (int)($row['dirs'] ?? 0),
            'bytes' => (int)($row['bytes'] ?? 0),
        ];
    }

    /* Verzeichnisbaum rekursiv loeschen: Dateien zuerst, Verzeichnisse bottom-up */
    private static function deleteTree(string $dir): void
    {
        foreach (scandir($dir) as $item) {
            if ($item === '.' || $item === '..') {
                continue;
            }
            $fp = $dir . '/' . $item;
            if (is_dir($fp) && !is_link($fp)) {
                self::deleteTree($fp);
            } else {
                unlink($fp);  /* Symlink: nur den Link entfernen, nie das Ziel */
            }
        }
        rmdir($dir);
    }

    /* ------------------------------------------------------------------ */
    public static function info(string $path): void
    {
        $target = storagePath($path);

        if (!file_exists($target)) {
            Response::error(404, 'Not found');
        }

        $isDir  = is_dir($target);
        $dbP    = dbPath($path);
        $map    = self::metaMap([$dbP]);
        $mtime  = (int)filemtime($target);
        Response::ok(self::withMeta([
            'name'     => basename($target),
            'type'     => $isDir ? 'dir' : 'file',
            'size'     => $isDir ? self::dirSize($target) : (int)filesize($target),
            'mime'     => $isDir ? 'inode/directory' : (mime_content_type($target) ?: 'application/octet-stream'),
            'created'  => localDateStr((int)filectime($target)),
            'modified' => localDateStr($mtime),
            'mtime'    => $mtime,
            'md5'      => $isDir ? '' : self::md5For($target, $dbP, $map),
        ], $map, $dbP));
    }

    /* Rekursive Gesamtgroesse aller Dateien unterhalb von $dir (in Bytes) */
    private static function dirSize(string $dir): int
    {
        $size = 0;
        $it = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator($dir, FilesystemIterator::SKIP_DOTS)
        );
        foreach ($it as $file) {
            if ($file->isFile() && !$file->isLink()) {
                $size += $file->getSize();
            }
        }
        return $size;
    }

    /* ------------------------------------------------------------------ */
    /* Delta-Quelle fuer den Client (Sync-Metadaten, docs/api/API.md "changes"):
     * alle Zeilen dieses Users mit change_seq > $since, lebend wie
     * geloescht — der Client erkennt Loeschungen am 'deleted'-Flag statt
     * eine fehlende Zeile mit "neu auf der anderen Seite" zu verwechseln.
     * 'path' hat (anders als list()'s 'name') immer einen fuehrenden '/',
     * da die Eintraege nicht auf ein Verzeichnis beschraenkt sind. */
    public static function changes(int $since): void
    {
        $since = max(0, $since);
        $stmt  = DB::get()->prepare(
            'SELECT id, path, is_dir, size_bytes, checksum, revision, change_seq,
                    UNIX_TIMESTAMP(modified_at) AS mtime_ts, deleted_at
             FROM files
             WHERE user_id = ? AND change_seq > ?
             ORDER BY change_seq ASC'
        );
        $stmt->execute([Auth::$user_id, $since]);

        $entries = [];
        foreach ($stmt->fetchAll() as $row) {
            $entries[] = [
                'path'       => '/' . $row['path'],
                'type'       => ((int)$row['is_dir'] === 1) ? 'dir' : 'file',
                'size'       => (int)$row['size_bytes'],
                'md5'        => (string)($row['checksum'] ?? ''),
                'mtime'      => (int)($row['mtime_ts'] ?? 0),
                'revision'   => (int)$row['revision'],
                'change_seq' => (int)$row['change_seq'],
                'id'         => (int)$row['id'],
                'deleted'    => $row['deleted_at'] !== null,
            ];
        }

        Response::ok([
            'global_revision' => self::currentGlobalRevision(),
            'entries'         => $entries,
        ]);
    }

    /* ------------------------------------------------------------------ */
    /* Wandelt ein "*"/"?"/"#?"-Joker-Muster (pattern.h-Konvention, siehe
     * core/libcloudfs) in ein SQL-LIKE-Muster um. Reihenfolge wichtig:
     * "#?" muss vor dem einzelnen "?" behandelt werden, sonst wuerde das
     * "?" darin faelschlich als Einzelzeichen-Joker uebersetzt. Literale
     * LIKE-Sonderzeichen (%, _, \) im Rest werden per likeEscape()
     * maskiert, bevor die Joker eingesetzt werden. */
    private static function globToLike(string $glob): string
    {
        $len  = strlen($glob);
        $like = '';
        for ($i = 0; $i < $len; $i++) {
            if ($glob[$i] === '#' && $i + 1 < $len && $glob[$i + 1] === '?') {
                $like .= '%';
                $i++;
            } elseif ($glob[$i] === '*') {
                $like .= '%';
            } elseif ($glob[$i] === '?') {
                $like .= '_';
            } else {
                $like .= self::likeEscape($glob[$i]);
            }
        }
        return $like;
    }

    /* Server-seitige Suche (docs/design/search-plan.md): matcht das
     * Joker-Muster $glob gegen den Basename (letztes Pfadsegment) aller
     * lebenden Zeilen von Auth::$user_id unterhalb von $path — ein
     * einziger Request statt Verzeichnis-fuer-Verzeichnis vom Client aus.
     * $dirsToo=false liefert nur Dateien (Commander-"auch Ordner"-Checkbox). */
    public static function search(string $path, string $glob, bool $dirsToo): void
    {
        if ($glob === '') {
            Response::error(400, 'search requires q');
            return;
        }
        $entries = self::searchRaw($path, $glob, $dirsToo);
        Response::ok([
            'entries'   => $entries,
            'truncated' => count($entries) === 2000,
        ]);
    }

    /* Wie search(), aber als Array fuer die Weboberflaeche */
    public static function searchRaw(string $path, string $glob, bool $dirsToo): array
    {
        $scope = dbPath($path);
        $like  = self::globToLike($glob);

        $sql = 'SELECT id, path, is_dir, size_bytes, checksum, revision,
                       change_seq, UNIX_TIMESTAMP(modified_at) AS mtime_ts
                FROM files
                WHERE user_id = ? AND deleted_at IS NULL
                  AND CONVERT(SUBSTRING_INDEX(path, "/", -1) USING utf8mb4) LIKE ? ESCAPE \'\\\\\' ';
        $args = [Auth::$user_id, $like];

        if ($scope !== '') {
            $sql   .= 'AND path LIKE ? ESCAPE \'\\\\\' ';
            $args[] = self::likeEscape($scope) . '/%';
        }
        if (!$dirsToo) {
            $sql .= 'AND is_dir = 0 ';
        }
        $sql .= 'ORDER BY path ASC LIMIT 2000';

        $stmt = DB::get()->prepare($sql);
        $stmt->execute($args);

        $entries = [];
        foreach ($stmt->fetchAll() as $row) {
            $entries[] = [
                'path'       => '/' . $row['path'],
                'type'       => ((int)$row['is_dir'] === 1) ? 'dir' : 'file',
                'size'       => (int)$row['size_bytes'],
                'md5'        => (string)($row['checksum'] ?? ''),
                'mtime'      => (int)($row['mtime_ts'] ?? 0),
                'revision'   => (int)$row['revision'],
                'change_seq' => (int)$row['change_seq'],
                'id'         => (int)$row['id'],
            ];
        }
        return $entries;
    }

    /* Die zuletzt geaenderten Dateien (Web: "Zuletzt geaendert") */
    public static function recentRaw(int $limit = 50): array
    {
        $stmt = DB::get()->prepare(
            'SELECT path, size_bytes, checksum, UNIX_TIMESTAMP(modified_at) AS mtime_ts
               FROM files WHERE user_id = ? AND deleted_at IS NULL AND is_dir = 0
              ORDER BY modified_at DESC, id DESC LIMIT ' . max(1, min(500, $limit))
        );
        $stmt->execute([Auth::$user_id]);
        $out = [];
        foreach ($stmt->fetchAll() as $row) {
            if (!is_file(storagePath('/' . $row['path']))) {
                continue;
            }
            $out[] = [
                'path'  => '/' . $row['path'],
                'type'  => 'file',
                'size'  => (int)$row['size_bytes'],
                'md5'   => (string)($row['checksum'] ?? ''),
                'mtime' => (int)$row['mtime_ts'],
            ];
        }
        return $out;
    }

    /* Unterordner von $path aus dem Dateisystem (nicht aus der DB: implizit
     * angelegte Elternordner haben keine files-Zeile) -- fuer den
     * Zielordner-Dialog "Verschieben". */
    public static function subdirs(string $path): array
    {
        $dir = storagePath($path);
        if (!is_dir($dir)) {
            return [];
        }
        $out = [];
        foreach (scandir($dir) as $item) {
            if ($item !== '.' && $item !== '..' && is_dir($dir . '/' . $item)) {
                $out[] = $item;
            }
        }
        natcasesort($out);
        return array_values($out);
    }

    /* Metadaten einer Datei/eines Ordners fuer die Details-Seitenleiste */
    public static function infoRaw(string $path): ?array
    {
        $target = storagePath($path);
        if ($path === '/' || !file_exists($target)) {
            return null;
        }
        $isDir = is_dir($target);
        $dbP   = dbPath($path);
        $map   = self::metaMap([$dbP]);
        $mtime = (int)filemtime($target);
        return self::withMeta([
            'name'     => basename($target),
            'type'     => $isDir ? 'dir' : 'file',
            'size'     => $isDir ? self::dirSize($target) : (int)filesize($target),
            'modified' => localDateStr($mtime),
            'mtime'    => $mtime,
            'md5'      => $isDir ? '' : self::md5For($target, $dbP, $map),
        ], $map, $dbP);
    }

    public static function setcommentRaw(string $path, string $comment): bool
    {
        $comment = mb_substr(preg_replace('/[\x00-\x1f\x7f]/', '', $comment), 0, 79, 'UTF-8');
        return self::metaImpl($path, 'comment', $comment)[0] === 0;
    }

    /* ------------------------------------------------------------------ */
    public static function mkdir(string $path): void
    {
        $dir = storagePath($path);

        if (is_dir($dir)) {
            Response::error(409, 'Directory already exists');
        }
        if (!mkdir($dir, 0755, true)) {
            Response::error(500, 'Cannot create directory');
        }

        /* change_seq bumpen, damit eine neu angelegte (insb. leere)
         * Verzeichnis-Zeile ueber action=changes auffindbar ist — ohne das
         * waere ein leeres Verzeichnis fuer den Delta-Sync unsichtbar,
         * bis irgendeine Datei darunter hochgeladen wird. ON DUPLICATE
         * KEY UPDATE statt INSERT IGNORE, damit ein zuvor geloeschtes
         * Verzeichnis (deleted_at gesetzt) beim Neuanlegen wiederbelebt
         * wird, statt eine Tombstone-Zeile stehen zu lassen. */
        $changeSeq = self::bumpGlobalRevision();
        DB::get()->prepare(
            'INSERT INTO files (user_id, path, size_bytes, is_dir, change_seq, deleted_at)
             VALUES (?, ?, 0, 1, ?, NULL)
             ON DUPLICATE KEY UPDATE change_seq = VALUES(change_seq), deleted_at = NULL'
        )->execute([Auth::$user_id, dbPath($path), $changeSeq]);

        Response::ok(['path' => $path, 'change_seq' => $changeSeq]);
    }

    /* ------------------------------------------------------------------ */
    /* rename: Datei oder Verzeichnis (rekursives DB-Praefix-Update)       */

    private static function renameImpl(string $from, string $to): array
    {
        if ($from === '/' || $to === '/') {
            return [400, 'Cannot rename root'];
        }
        $src = storagePath($from);
        $dst = storagePath($to);

        if (!file_exists($src)) {
            return [404, 'Source not found'];
        }
        if (file_exists($dst)) {
            return [409, 'Target already exists'];
        }
        if (strpos($to . '/', rtrim($from, '/') . '/') === 0) {
            return [400, 'Cannot move a directory into itself'];
        }
        if (!is_dir(dirname($dst))) {
            return [404, 'Target parent directory not found'];
        }

        $isDir = is_dir($src);
        if (!@rename($src, $dst)) {
            return [500, 'Rename failed'];
        }

        /* Umbenennen/Verschieben aendert nur den Pfad, nie die Revision
         * (Sync-Metadaten, docs/api/API.md "changes") — change_seq wird trotzdem einmal pro
         * Aufruf gebumpt, damit action=changes den neuen Pfad meldet.
         * Tombstones am Ziel (und darunter) muessen vorher weg: uq_user_path
         * umfasst auch geloeschte Zeilen, das UPDATE liefe sonst nach dem
         * schon erfolgten rename() im Dateisystem auf einen Duplicate Key. */
        $fromDb = dbPath($from);
        $toDb   = dbPath($to);
        $db = DB::get();
        $db->beginTransaction();
        try {
            $db->prepare('DELETE FROM files WHERE user_id = ? AND deleted_at IS NOT NULL AND (path = ? OR path LIKE ?)')
               ->execute([Auth::$user_id, $toDb, self::likeEscape($toDb) . '/%']);

            $changeSeq = self::bumpGlobalRevision();
            $db->prepare('UPDATE files SET path = ?, change_seq = ? WHERE user_id = ? AND path = ?')
               ->execute([$toDb, $changeSeq, Auth::$user_id, $fromDb]);

            if ($isDir) {
                /* Alle Kind-Pfade umschreiben: alter Praefix -> neuer Praefix */
                $oldPrefix = $fromDb . '/';
                $newPrefix = $toDb . '/';
                $db->prepare(
                    'UPDATE files SET path = CONCAT(?, SUBSTRING(path, ?)), change_seq = ?
                     WHERE user_id = ? AND path LIKE ?'
                )->execute([
                    $newPrefix,
                    strlen($oldPrefix) + 1,
                    $changeSeq,
                    Auth::$user_id,
                    self::likeEscape($oldPrefix) . '%',
                ]);
            }
            Shares::onRename(Auth::$user_id, $fromDb, $toDb);
            $db->commit();
        } catch (Throwable $e) {
            $db->rollBack();
            throw $e;
        }
        return [0, ''];
    }

    public static function rename(string $from, string $to): void
    {
        [$code, $msg] = self::renameImpl($from, $to);
        if ($code !== 0) {
            Response::error($code, $msg);
        }
        Response::ok(['path' => $from, 'to' => $to]);
    }

    /* ------------------------------------------------------------------ */
    /* Amiga-Metadaten: Kommentar, Protection-Bits, Datum                  */

    public static function setcomment(string $path, string $comment): void
    {
        /* Steuerzeichen raus, Amiga-Limit 79 Zeichen */
        $comment = mb_substr(preg_replace('/[\x00-\x1f\x7f]/', '', $comment), 0, 79, 'UTF-8');
        [$code, $msg] = self::metaImpl($path, 'comment', $comment);
        if ($code !== 0) {
            Response::error($code, $msg);
        }
        Response::ok(['path' => $path, 'comment' => $comment]);
    }

    public static function setprotect(string $path, int $bits): void
    {
        if ($bits < 0) {
            Response::error(400, 'Invalid protection bits');
        }
        [$code, $msg] = self::metaImpl($path, 'protection', $bits);
        if ($code !== 0) {
            Response::error($code, $msg);
        }
        Response::ok(['path' => $path, 'protection' => $bits]);
    }

    /* $epoch (UTC-Sekunden) hat Vorrang vor $iso, falls beide/keines
     * angegeben sind entscheidet der Aufrufer (api.php) anhand von
     * &mtime= vs. &modified= — siehe Abschnitt 19. Zeitzonenfrei: der
     * Client (insbesondere sync, Abschnitt 20) kann so die Remote-Mtime
     * exakt auf die Quelldatei setzen, ohne Server-Zeitzonen-Umrechnung. */
    public static function setdate(string $path, ?string $iso, ?int $epoch): void
    {
        if ($epoch !== null) {
            $dt = new DateTime('@' . $epoch);
            $dt->setTimezone(new DateTimeZone(date_default_timezone_get()));
        } else {
            /* Kein explizites DateTimeZone-Argument: createFromFormat()
             * nimmt dann die oben per date_default_timezone_set() gesetzte
             * lokale Zeitzone — dieselbe, in der auch localDateStr() ausgibt. */
            $dt = DateTime::createFromFormat('Y-m-d H:i:s', (string)$iso);
            if ($dt === false) {
                Response::error(400, "Invalid date, expected 'YYYY-MM-DD HH:MM:SS' (2026-01-31 12:00:00) or &mtime=<epoch>");
            }
        }

        $isoOut = $dt->format('Y-m-d H:i:s');
        [$code, $msg] = self::metaImpl($path, 'modified_at', $isoOut);
        if ($code !== 0) {
            Response::error($code, $msg);
        }
        /* FS-mtime mitziehen — list/info lesen modified aus dem Filesystem */
        @touch(storagePath($path), $dt->getTimestamp());
        Response::ok(['path' => $path, 'modified' => $isoOut, 'mtime' => $dt->getTimestamp()]);
    }

    /* ══ Web-interface variants (return data, don't output JSON) ══════ */

    public static function renameRaw(string $from, string $to): bool
    {
        [$code, ] = self::renameImpl($from, $to);
        return $code === 0;
    }

    public static function listRaw(string $path): array
    {
        $dir = storagePath($path);

        if ($path === '/' && !is_dir($dir)) {
            mkdir($dir, 0755, true);
        }
        if (!is_dir($dir)) {
            return [];
        }
        return self::scanEntries($dir, $path);
    }

    /* Web-Upload: derselbe Schreibweg wie die API. Rueckgabe null bei
     * Erfolg, sonst die Fehlermeldung (507 -> Kontingent). */
    public static function uploadRaw(string $path, string $tmpFile): ?string
    {
        $r = self::uploadImpl($path, null, $tmpFile, null);
        if ($r[0] === 0) {
            return null;
        }
        return $r[0] === 507 ? 'Speicherkontingent überschritten' : $r[1];
    }

    public static function mkdirRaw(string $path): bool
    {
        $dir = storagePath($path);
        if (is_dir($dir)) {
            return false;
        }
        if (!mkdir($dir, 0755, true)) {
            return false;
        }

        $changeSeq = self::bumpGlobalRevision();
        DB::get()->prepare(
            'INSERT INTO files (user_id, path, size_bytes, is_dir, change_seq, deleted_at)
             VALUES (?, ?, 0, 1, ?, NULL)
             ON DUPLICATE KEY UPDATE change_seq = VALUES(change_seq), deleted_at = NULL'
        )->execute([Auth::$user_id, dbPath($path), $changeSeq]);

        return true;
    }

    public static function downloadRaw(string $path): void
    {
        $file = storagePath($path);
        if (!is_file($file)) {
            http_response_code(404);
            exit;
        }
        self::sendAttachment($file, basename($file));
    }

    /* Datei immer als Download ausliefern, nie inline: hochgeladenes
     * HTML/SVG darf unter der eigenen Origin nicht ausgefuehrt werden
     * (es kaeme an Session-Cookie und CSRF-Token heran). Auch share.php. */
    public static function sendAttachment(string $file, string $name): void
    {
        $ascii = preg_replace('/[^\x20-\x7e]|["\\\\]/', '_', $name);
        header('Content-Type: application/octet-stream');
        header('Content-Length: ' . filesize($file));
        header('Content-Disposition: attachment; filename="' . $ascii . '"; filename*=UTF-8\'\'' . rawurlencode($name));
        header('X-Content-Type-Options: nosniff');
        header("Content-Security-Policy: sandbox; default-src 'none'");
        readfile($file);
        exit;
    }
}
