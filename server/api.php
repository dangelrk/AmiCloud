<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
define('AMICLOUD', 1);

require __DIR__ . '/config.php';

/* Enforce HTTPS — refuse plain HTTP connections.
 * AMICLOUD_ALLOW_HTTP=true in config.php erlaubt HTTP nur fuer lokale
 * Entwicklung (php -S kann kein TLS) — in Produktion nie setzen. */
$https = $_SERVER['HTTPS'] ?? '';
if ((empty($https) || $https === 'off')
    && !(defined('AMICLOUD_ALLOW_HTTP') && AMICLOUD_ALLOW_HTTP)) {
    http_response_code(403);
    header('Content-Type: application/json');
    echo '{"status":"error","code":403,"message":"HTTPS required"}';
    exit;
}

require __DIR__ . '/includes/Version.php';
require __DIR__ . '/includes/Response.php';
require __DIR__ . '/includes/DB.php';
require __DIR__ . '/includes/Throttle.php';
require __DIR__ . '/includes/Users.php';
require __DIR__ . '/includes/AppPasswords.php';
require __DIR__ . '/includes/Shares.php';
require __DIR__ . '/includes/Auth.php';
require __DIR__ . '/includes/FileStore.php';

header('X-Content-Type-Options: nosniff');
header('X-Frame-Options: DENY');

$action = trim($_GET['action'] ?? '');

/* Die Clients (Amiga/PC) schicken nie Sec-Fetch-*-Header, Browser schon.
 * Hat ein Browser die Basic-Auth-Daten zwischengespeichert, koennte ein
 * Link oder Formular auf einer fremden Seite sonst Dateien loeschen oder
 * aendern (alle Aktionen sind GET/POST ohne Token). Aendernde Aktionen
 * aus dem Browser werden deshalb nur bei direkter Eingabe zugelassen. */
$fetchSite = (string)($_SERVER['HTTP_SEC_FETCH_SITE'] ?? '');
if ($fetchSite !== '' && $fetchSite !== 'none'
    && in_array($action, ['upload', 'delete', 'mkdir', 'rename', 'setcomment', 'setprotect', 'setdate'], true)) {
    Response::error(403, 'Cross-site requests are not allowed');
}

Auth::requireBasicAuth();

$path   = sanitizePath($_GET['path'] ?? '/');

switch ($action) {
    case 'status':   Response::ok([
                         'version'         => AMICLOUD_VERSION,
                         'time'            => localDateStr(time()),
                         'global_revision' => FileStore::currentGlobalRevision(),
                         'user'            => [
                             'name'         => Auth::$username,
                             'display_name' => Users::displayName(Auth::$user),
                         ],
                         'quota'           => Users::quotaInfo(Auth::$user),
                         'max_upload'      => MAX_UPLOAD,
                     ]); break;
    case 'changes':  FileStore::changes(
                         (int)($_GET['since'] ?? 0),
                         (int)($_GET['limit'] ?? 0),
                         $path   /* Praefix: nur Eintraege darunter, "/" = alle */
                     ); break;
    case 'search':   FileStore::search($path, trim($_GET['q'] ?? ''),
                         isset($_GET['dirs']) ? (bool)$_GET['dirs'] : true); break;
    case 'list':     FileStore::list($path); break;
    case 'download': FileStore::download(
                         $path,
                         max(0, (int)($_GET['offset'] ?? 0)),
                         isset($_GET['length']) ? (int)$_GET['length'] : -1
                     ); break;
    case 'upload':   FileStore::upload(
                         $path,
                         (string)file_get_contents('php://input'),
                         isset($_GET['md5']) ? (string)$_GET['md5'] : null,
                         isset($_GET['if_revision']) ? (int)$_GET['if_revision'] : null,
                         !empty($_GET['create_only']),
                         isset($_GET['if_id']) ? (int)$_GET['if_id'] : null
                     ); break;
    case 'delete':   FileStore::delete(
                         $path,
                         (bool)($_GET['recursive'] ?? false),
                         isset($_GET['if_revision']) ? (int)$_GET['if_revision'] : null,
                         isset($_GET['if_id']) ? (int)$_GET['if_id'] : null
                     ); break;
    case 'info':     FileStore::info($path); break;
    case 'mkdir':    FileStore::mkdir($path); break;
    case 'rename':   FileStore::rename($path, sanitizePath($_GET['to'] ?? '')); break;
    case 'setcomment': FileStore::setcomment($path, (string)($_GET['comment'] ?? '')); break;
    case 'setprotect': FileStore::setprotect($path, (int)($_GET['bits'] ?? -1)); break;
    case 'setdate':    FileStore::setdate(
                            $path,
                            isset($_GET['mtime']) ? null : trim($_GET['modified'] ?? ''),
                            isset($_GET['mtime']) ? (int)$_GET['mtime'] : null
                        ); break;
    default:         Response::error(400, 'Unknown action: ' . htmlspecialchars($action));
}
