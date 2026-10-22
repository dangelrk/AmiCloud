<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
define('AMICLOUD', 1);
require __DIR__ . '/config.php';

/* ── HTTPS enforce (AMICLOUD_ALLOW_HTTP: nur lokale Entwicklung) ───────
 * Vor session_start(), damit nie ein Session-Cookie ueber HTTP entsteht. */
$https = !empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off';
if (!$https && !(defined('AMICLOUD_ALLOW_HTTP') && AMICLOUD_ALLOW_HTTP)) {
    header('Location: https://' . $_SERVER['HTTP_HOST'] . $_SERVER['REQUEST_URI']);
    exit;
}
session_set_cookie_params([
    'lifetime' => 0,
    'path'     => '/',
    'secure'   => $https,
    'httponly' => true,
    'samesite' => 'Strict',
]);
session_start();
require __DIR__ . '/includes/Version.php';
require __DIR__ . '/includes/Response.php';
require __DIR__ . '/includes/DB.php';
require __DIR__ . '/includes/Throttle.php';
require __DIR__ . '/includes/Users.php';
require __DIR__ . '/includes/AppPasswords.php';
require __DIR__ . '/includes/Shares.php';
require __DIR__ . '/includes/Auth.php';
require __DIR__ . '/includes/FileStore.php';
require __DIR__ . '/includes/Zip.php';
require __DIR__ . '/includes/web/helpers.php';
require __DIR__ . '/includes/web/layout.php';
require __DIR__ . '/includes/web/files.php';
require __DIR__ . '/includes/web/settings.php';
require __DIR__ . '/includes/web/users.php';

/* Weboberflaeche (ownCloud-Bedienkonzept, docs/design/user-management-plan.md):
 * GET ?view=files|recent|search|shares|settings|users, GET ?do=download,
 * GET ?ajax=dirs|info, alle aendernden Aktionen per POST do=... mit CSRF. */

header('X-Content-Type-Options: nosniff');
header('Referrer-Policy: same-origin');
header('X-Frame-Options: SAMEORIGIN');

if (empty($_SESSION['csrf'])) {
    $_SESSION['csrf'] = bin2hex(random_bytes(16));
}
$isPost = $_SERVER['REQUEST_METHOD'] === 'POST';
$do     = (string)($_GET['do'] ?? $_POST['do'] ?? '');
$isAjax = ($_SERVER['HTTP_X_REQUESTED_WITH'] ?? '') === 'fetch';

if ($isPost && !hash_equals($_SESSION['csrf'], (string)($_POST['csrf'] ?? ''))) {
    if ($isAjax) {
        Response::error(403, 'Sitzung abgelaufen – bitte Seite neu laden.');
    }
    flash('Die Sitzung ist abgelaufen. Bitte erneut versuchen.', 'error');
    redirect('/web.php');
}

/* ── Anmelden ────────────────────────────────────────────────────────── */
if ($isPost && $do === 'login') {
    $r = Auth::webLogin((string)($_POST['username'] ?? ''), (string)($_POST['password'] ?? ''));
    if (is_array($r)) {
        Auth::startSession($r);
        redirect('/web.php');
    }
    renderLogin($r);
    exit;
}

if (!Auth::loadSessionUser()) {
    if ($isAjax) {
        Response::error(401, 'Nicht angemeldet.');
    }
    renderLogin('');
    exit;
}
$uid = Auth::$user_id;

/* Rueckkehr-URL aus dem Formular, nur innerhalb von web.php */
function backUrl(string $fallback = '/web.php'): string
{
    $b = (string)($_POST['back'] ?? '');
    return preg_match('#^/web\.php(\?[^\r\n]*)?$#', $b) ? $b : $fallback;
}

function postedPaths(): array
{
    $raw = $_POST['paths'] ?? [];
    if (!is_array($raw)) {
        $raw = [$raw];
    }
    $out = [];
    foreach ($raw as $p) {
        $p = sanitizePath((string)$p);
        if ($p !== '/') {
            $out[] = $p;
        }
    }
    return array_values(array_unique($out));
}

function requireAdmin(): void
{
    if (!Auth::$is_admin) {
        http_response_code(403);
        exit('Nur für Administratoren.');
    }
}

function postedQuota()
{
    $preset = (string)($_POST['quota_preset'] ?? '');
    if ($preset === 'custom') {
        return Users::parseSize((string)($_POST['quota_custom'] ?? ''));
    }
    return $preset === '' ? null : (int)$preset;
}

/* ── GET: Download / AJAX ────────────────────────────────────────────── */
if (!$isPost && $do === 'download') {
    FileStore::downloadRaw(sanitizePath((string)($_GET['path'] ?? '')));
}

if (!$isPost && isset($_GET['ajax'])) {
    $path = sanitizePath((string)($_GET['path'] ?? '/'));
    if ($_GET['ajax'] === 'dirs') {
        Response::ok(['path' => $path, 'dirs' => FileStore::subdirs($path)]);
    }
    if ($_GET['ajax'] === 'info') {
        $info = FileStore::infoRaw($path);
        if ($info === null) {
            Response::error(404, 'Nicht gefunden.');
        }
        $shares = [];
        foreach (Shares::forPath($uid, $path) as $s) {
            $shares[] = [
                'id'        => (int)$s['id'],
                'url'       => shareUrl($s['token']),
                'has_pass'  => (bool)$s['has_pass'],
                'expires'   => $s['expires_at'] ? date('d.m.Y', strtotime($s['expires_at'])) : '',
                'downloads' => (int)$s['downloads'],
            ];
        }
        Response::ok([
            'name'       => $info['name'],
            'type'       => $info['type'],
            'size'       => fmtSize($info['size']),
            'modified'   => date('d.m.Y H:i', $info['mtime']),
            'md5'        => $info['md5'],
            'comment'    => $info['comment'],
            'protection' => protectionStr((int)$info['protection']),
            'shares'     => $shares,
        ]);
    }
    Response::error(400, 'Unbekannte Anfrage.');
}

/* ── POST-Aktionen ───────────────────────────────────────────────────── */
if ($isPost) {
    switch ($do) {
    case 'logout':
        $_SESSION = [];
        session_destroy();
        redirect('/web.php');

    case 'upload':
        /* Eine Datei pro Request (XHR mit Fortschritt). relpath enthaelt
         * beim Ordner-Upload den Unterordner ("ordner/datei.ext"). */
        $dir  = sanitizePath((string)($_POST['dir'] ?? '/'));
        $f    = $_FILES['file'] ?? null;
        $rel  = trim(str_replace('\\', '/', (string)($_POST['relpath'] ?? ($f['name'] ?? ''))), '/');
        if (!$f || is_array($f['name'])) {
            Response::error(400, 'Keine Datei empfangen.');
        }
        if ($f['error'] === UPLOAD_ERR_INI_SIZE || $f['error'] === UPLOAD_ERR_FORM_SIZE) {
            Response::error(413, $rel . ': Datei ist zu groß für den Server.');
        }
        if ($f['error'] !== UPLOAD_ERR_OK || $rel === '') {
            Response::error(400, $rel . ': Hochladen fehlgeschlagen.');
        }
        $err = FileStore::uploadRaw(sanitizePath($dir . '/' . $rel), (string)$f['tmp_name']);
        if ($err !== null) {
            Response::error(400, $rel . ': ' . $err);
        }
        Response::ok(['name' => $rel]);

    case 'mkdir':
        $dir  = sanitizePath((string)($_POST['dir'] ?? '/'));
        $name = trim((string)($_POST['name'] ?? ''));
        if ($name === '' || strpos($name, '/') !== false || $name === '.' || $name === '..') {
            flash('Ungültiger Ordnername.', 'error');
        } elseif (!FileStore::mkdirRaw(sanitizePath($dir . '/' . $name))) {
            flash("Ordner „{$name}“ konnte nicht angelegt werden (existiert er schon?).", 'error');
        }
        redirect(webUrl(['path' => $dir]));

    case 'rename':
        $path = sanitizePath((string)($_POST['path'] ?? ''));
        $name = trim((string)($_POST['name'] ?? ''));
        if ($name === '' || strpos($name, '/') !== false || $name === '.' || $name === '..') {
            flash('Ungültiger Name.', 'error');
        } elseif ($name !== basename($path)
                  && !FileStore::renameRaw($path, sanitizePath(joinPath(parentPath($path), $name)))) {
            flash("Umbenennen in „{$name}“ fehlgeschlagen (Name schon vergeben?).", 'error');
        }
        redirect(backUrl());

    case 'move':
        $target = sanitizePath((string)($_POST['target'] ?? '/'));
        $ok = 0;
        $failed = [];
        foreach (postedPaths() as $p) {
            if (parentPath($p) === $target) {
                continue;
            }
            if (FileStore::renameRaw($p, joinPath($target, basename($p)))) {
                $ok++;
            } else {
                $failed[] = basename($p);
            }
        }
        if ($ok > 0) {
            flash($ok . ($ok === 1 ? ' Element' : ' Elemente') . " nach {$target} verschoben.");
        }
        if ($failed !== []) {
            flash('Nicht verschoben (Name im Ziel vergeben oder Ziel liegt im Ordner selbst): ' . implode(', ', $failed), 'error');
        }
        redirect(backUrl());

    case 'delete':
        $ok = 0;
        $failed = [];
        foreach (postedPaths() as $p) {
            if (FileStore::deleteRaw($p, true)) {
                $ok++;
            } else {
                $failed[] = basename($p);
            }
        }
        if ($ok > 0) {
            flash($ok . ($ok === 1 ? ' Element gelöscht.' : ' Elemente gelöscht.'));
        }
        if ($failed !== []) {
            flash('Konnte nicht gelöscht werden: ' . implode(', ', $failed), 'error');
        }
        redirect(backUrl());

    case 'zip':
        $paths = postedPaths();
        $name  = count($paths) === 1 ? basename($paths[0]) . '.zip' : 'AmiCloud-Auswahl.zip';
        flash(Zip::send($paths, $name), 'error');
        redirect(backUrl());

    case 'setcomment':
        $ok = FileStore::setcommentRaw(sanitizePath((string)($_POST['path'] ?? '')), (string)($_POST['comment'] ?? ''));
        if (!$ok) {
            Response::error(404, 'Nicht gefunden.');
        }
        Response::ok();

    case 'share_create':
        $path = sanitizePath((string)($_POST['path'] ?? ''));
        if (FileStore::infoRaw($path) === null) {
            Response::error(404, 'Nicht gefunden.');
        }
        [$token, $err] = Shares::create($uid, $path, (string)($_POST['pass'] ?? ''), trim((string)($_POST['expires'] ?? '')));
        if ($err !== null) {
            Response::error(400, $err);
        }
        Response::ok(['url' => shareUrl($token)]);

    case 'share_delete':
        Shares::delete($uid, (int)($_POST['id'] ?? 0));
        if ($isAjax) {
            Response::ok();
        }
        flash('Link gelöscht.');
        redirect(backUrl(webUrl(['view' => 'shares'])));

    case 'profile':
        $err = Users::updateProfile($uid, trim((string)($_POST['display_name'] ?? '')), trim((string)($_POST['email'] ?? '')));
        flash($err ?? 'Profil gespeichert.', $err ? 'error' : 'info');
        redirect(webUrl(['view' => 'settings']));

    case 'password':
        $new = (string)($_POST['new'] ?? '');
        $err = Auth::confirmPassword((string)($_POST['current'] ?? ''));
        if ($err === null && $new !== (string)($_POST['new2'] ?? '')) {
            $err = 'Die neuen Passwörter stimmen nicht überein.';
        }
        $err = $err ?? Users::setPassword($uid, $new);
        $msg = 'Passwort geändert. Andere Browser-Sitzungen wurden abgemeldet.';
        if ($err === null) {
            /* Eigene Session behalten, alle anderen sind ueber session_epoch raus */
            Auth::startSession(Users::find($uid));
            if (!empty($_POST['revoke_tokens'])) {
                $n   = AppPasswords::revokeAll($uid);
                $msg .= $n > 0 ? " $n App-Passwort/-Passwörter widerrufen." : '';
            }
        }
        flash($err ?? $msg, $err ? 'error' : 'info');
        redirect(webUrl(['view' => 'settings']));

    case 'apppw_create':
        $name = trim((string)($_POST['name'] ?? ''));
        /* Mit Kontopasswort: eine gestohlene Sitzung soll sich keinen
         * dauerhaften API-Zugang verschaffen koennen. */
        $err = Auth::confirmPassword((string)($_POST['current'] ?? ''));
        if ($err !== null) {
            flash($err, 'error');
            redirect(webUrl(['view' => 'settings']) . '#devices');
        }
        [$token, $err] = AppPasswords::create($uid, $name);
        if ($err !== null) {
            flash($err, 'error');
        } else {
            $_SESSION['new_token'] = ['name' => $name, 'token' => $token];
        }
        redirect(webUrl(['view' => 'settings']) . '#devices');

    case 'apppw_revoke':
        AppPasswords::revoke($uid, (int)($_POST['id'] ?? 0));
        flash('App-Passwort widerrufen.');
        redirect(webUrl(['view' => 'settings']) . '#devices');

    case 'user_create':
        requireAdmin();
        $quota = postedQuota();
        if ($quota === false) {
            flash('Ungültiges Kontingent.', 'error');
            redirect(webUrl(['view' => 'users']));
        }
        [$id, $err] = Users::create(
            trim((string)($_POST['username'] ?? '')), (string)($_POST['password'] ?? ''),
            trim((string)($_POST['display_name'] ?? '')), trim((string)($_POST['email'] ?? '')),
            $quota, !empty($_POST['is_admin'])
        );
        flash($err ?? 'Benutzer ' . trim((string)$_POST['username']) . ' angelegt.', $err ? 'error' : 'info');
        redirect(webUrl(['view' => 'users']));

    case 'user_update':
        requireAdmin();
        $id    = (int)($_POST['id'] ?? 0);
        $quota = postedQuota();
        $err   = $quota === false ? 'Ungültiges Kontingent.' : null;
        $err   = $err ?? Users::updateProfile($id, trim((string)($_POST['display_name'] ?? '')), trim((string)($_POST['email'] ?? '')));
        $err   = $err ?? Users::updateAdmin($id, $quota, !empty($_POST['is_admin']), !empty($_POST['active']));
        if ($err === null && (string)($_POST['password'] ?? '') !== '') {
            $err = Users::setPassword($id, (string)$_POST['password']);
            if ($err === null && !empty($_POST['revoke_tokens'])) {
                AppPasswords::revokeAll($id);
            }
            if ($err === null && $id === $uid) {
                Auth::startSession(Users::find($uid));
            }
        }
        flash($err ?? 'Änderungen gespeichert.', $err ? 'error' : 'info');
        redirect(webUrl(['view' => 'users']));

    case 'user_delete':
        requireAdmin();
        $row = Users::find((int)($_POST['id'] ?? 0));
        if ($row === null || (string)($_POST['confirm'] ?? '') !== $row['username']) {
            flash('Löschen abgebrochen: Der eingetippte Name stimmt nicht.', 'error');
        } else {
            $err = Users::delete((int)$row['id'], $uid);
            flash($err ?? "Benutzer {$row['username']} gelöscht.", $err ? 'error' : 'info');
        }
        redirect(webUrl(['view' => 'users']));

    default:
        http_response_code(400);
        exit('Unbekannte Aktion.');
    }
}

/* ── Ansichten ───────────────────────────────────────────────────────── */

/* Eintraege der Listen auf eine Form bringen; Ordner bekommen Anzahl und
 * Groesse fuer die Loesch-Rueckfrage (eine DB-Abfrage je Ordner). */
function normalizeEntries(array $raw, string $dir = ''): array
{
    $out = [];
    foreach ($raw as $e) {
        $path  = $e['path'] ?? joinPath($dir, $e['name']);
        $isDir = $e['type'] === 'dir';
        $out[] = [
            'name'    => $e['name'] ?? basename($path),
            'path'    => $path,
            'type'    => $e['type'],
            'size'    => (int)$e['size'],
            'mtime'   => (int)$e['mtime'],
            'comment' => (string)($e['comment'] ?? ''),
            'counts'  => $isDir ? FileStore::countAndSize($path)
                                : ['files' => 1, 'dirs' => 0, 'bytes' => (int)$e['size']],
        ];
    }
    return $out;
}

$view = (string)($_GET['view'] ?? 'files');
$sort = [
    'by'           => in_array($_GET['sort'] ?? '', ['name', 'size', 'modified'], true) ? $_GET['sort'] : 'name',
    'dir'          => ($_GET['dir'] ?? '') === 'desc' ? 'desc' : 'asc',
    'foldersFirst' => ($_GET['foldersfirst'] ?? '1') !== '0',
];

switch ($view) {
case 'recent':
    renderLayout('Zuletzt geändert', 'recent',
        renderFiles('recent', '/', normalizeEntries(FileStore::recentRaw(50)), $sort));
    break;

case 'search':
    $q = trim((string)($_GET['q'] ?? ''));
    if ($q === '') {
        redirect('/web.php');
    }
    /* Ohne Joker wie ownCloud als Teilwortsuche */
    $glob = strpbrk($q, '*?') === false ? '*' . $q . '*' : $q;
    renderLayout('Suche', 'files',
        renderFiles('search', '/', normalizeEntries(FileStore::searchRaw('/', $glob, true)), $sort, $q), $q);
    break;

case 'shares':
    renderLayout('Freigaben', 'shares', renderShares());
    break;

case 'settings':
    $newToken = $_SESSION['new_token'] ?? null;
    unset($_SESSION['new_token']);
    renderLayout('Einstellungen', 'settings', renderSettings($newToken));
    break;

case 'users':
    requireAdmin();
    renderLayout('Benutzer', 'users', renderUsers());
    break;

default:
    $path = sanitizePath((string)($_GET['path'] ?? '/'));
    if ($path !== '/' && !is_dir(storagePath($path))) {
        flash("Ordner {$path} gibt es nicht.", 'error');
        redirect('/web.php');
    }
    $entries = normalizeEntries(FileStore::listRaw($path), $path);
    usort($entries, function (array $a, array $b) use ($sort): int {
        if ($sort['foldersFirst'] && ($a['type'] === 'dir') !== ($b['type'] === 'dir')) {
            return $a['type'] === 'dir' ? -1 : 1;
        }
        switch ($sort['by']) {
            case 'size':     $cmp = $a['counts']['bytes'] <=> $b['counts']['bytes']; break;
            case 'modified': $cmp = $a['mtime'] <=> $b['mtime']; break;
            default:         $cmp = strnatcasecmp($a['name'], $b['name']);
        }
        return $sort['dir'] === 'desc' ? -$cmp : $cmp;
    });
    renderLayout($path === '/' ? 'Alle Dateien' : basename($path), 'files', renderFiles('files', $path, $entries, $sort));
}
