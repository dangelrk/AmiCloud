<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
/* AmiCloud-Verwaltung auf der Kommandozeile (wie "occ" bei ownCloud).
 * Als Benutzer des Webservers aufrufen, damit angelegte/geloeschte
 * Speicherordner die richtigen Rechte haben:
 *
 *   sudo -u apache php bin/amicloud-admin.php user:add --admin ronny
 *
 * Der Ordner bin/ ist per .htaccess fuer den Browser gesperrt. */
if (PHP_SAPI !== 'cli') {
    http_response_code(403);
    exit;
}
define('AMICLOUD', 1);

$root = dirname(__DIR__);
if (!is_readable($root . '/config.php')) {
    fwrite(STDERR, "config.php nicht lesbar – als Webserver-Benutzer aufrufen (sudo -u apache …).\n");
    exit(1);
}
require $root . '/config.php';
require $root . '/includes/Version.php';
require $root . '/includes/Response.php';
require $root . '/includes/DB.php';
require $root . '/includes/Throttle.php';
require $root . '/includes/Users.php';
require $root . '/includes/AppPasswords.php';
require $root . '/includes/Shares.php';
require $root . '/includes/Auth.php';
require $root . '/includes/FileStore.php';

function usage(): void
{
    echo <<<TXT
AmiCloud v{$GLOBALS['ver']} – Verwaltung

  user:list
  user:add [--admin] [--quota=5G] [--display=NAME] [--email=ADR] [--password-stdin] <name>
  user:passwd [--password-stdin] [--keep-tokens] <name>
                                           widerruft auch die App-Passwörter
  user:revoke-tokens <name>                alle App-Passwörter widerrufen
  user:admin <name> on|off
  user:quota <name> <größe|unbegrenzt>      z. B. 500M, 5G
  user:longnames <name> on|off             lange Dateinamen erlauben (off = Amiga-Modus, max. 30 Zeichen)
  user:disable <name>
  user:enable <name>
  user:delete [--yes] <name>               löscht auch alle Dateien

TXT;
}

function fail(string $msg): void
{
    fwrite(STDERR, $msg . "\n");
    exit(1);
}

function fmtBytes(int $b): string
{
    if ($b < 1024)       return $b . ' B';
    if ($b < 1048576)    return round($b / 1024, 1) . ' KB';
    if ($b < 1073741824) return round($b / 1048576, 1) . ' MB';
    return round($b / 1073741824, 1) . ' GB';
}

/* Passwort ohne Echo lesen (bzw. eine Zeile von stdin) */
function readPassword(bool $fromStdin, string $prompt): string
{
    if ($fromStdin) {
        return rtrim((string)fgets(STDIN), "\r\n");
    }
    fwrite(STDOUT, $prompt);
    $tty = function_exists('posix_isatty') ? posix_isatty(STDIN) : true;
    if ($tty) {
        shell_exec('stty -echo');
    }
    $p = rtrim((string)fgets(STDIN), "\r\n");
    if ($tty) {
        shell_exec('stty echo');
        fwrite(STDOUT, "\n");
    }
    return $p;
}

function askNewPassword(bool $fromStdin): string
{
    $p = readPassword($fromStdin, 'Passwort: ');
    if (!$fromStdin && readPassword(false, 'Passwort wiederholen: ') !== $p) {
        fail('Die Passwörter stimmen nicht überein.');
    }
    return $p;
}

function userOrFail(string $name): array
{
    $row = Users::byName($name);
    if ($row === null) {
        fail("Benutzer $name nicht gefunden.");
    }
    return $row;
}

$ver  = AMICLOUD_VERSION;
$args = array_slice($argv, 1);
$cmd  = array_shift($args) ?? '';
$opts = [];
$pos  = [];
foreach ($args as $a) {
    if (preg_match('/^--([a-z-]+)(?:=(.*))?$/', $a, $m)) {
        $opts[$m[1]] = $m[2] ?? true;
    } else {
        $pos[] = $a;
    }
}
$stdinPw = isset($opts['password-stdin']);

switch ($cmd) {
case 'user:list':
    printf("%-20s %-24s %-6s %-6s %-22s %s\n", 'BENUTZER', 'ANZEIGENAME', 'ADMIN', 'AKTIV', 'BELEGT/KONTINGENT', 'LETZTER LOGIN');
    foreach (Users::all() as $u) {
        $q = $u['quota_bytes'] === null ? 'unbegrenzt' : fmtBytes((int)$u['quota_bytes']);
        printf("%-20s %-24s %-6s %-6s %-22s %s\n",
            $u['username'], mb_strimwidth($u['display_name'], 0, 24, '…', 'UTF-8'),
            $u['is_admin'] ? 'ja' : '-', $u['active'] ? 'ja' : 'nein',
            fmtBytes((int)$u['used_bytes']) . ' / ' . $q, $u['last_login'] ?? '-');
    }
    break;

case 'user:add':
    if (count($pos) !== 1) { usage(); exit(1); }
    $quota = null;
    if (isset($opts['quota'])) {
        $quota = Users::parseSize((string)$opts['quota']);
        if ($quota === false) fail('Ungültiges Kontingent: ' . $opts['quota']);
    }
    [$id, $err] = Users::create($pos[0], askNewPassword($stdinPw),
        (string)($opts['display'] ?? ''), (string)($opts['email'] ?? ''), $quota, isset($opts['admin']));
    if ($err !== null) fail($err);
    echo "Benutzer {$pos[0]} angelegt" . (isset($opts['admin']) ? ' (Administrator)' : '') . ".\n";
    break;

case 'user:passwd':
    if (count($pos) !== 1) { usage(); exit(1); }
    $u = userOrFail($pos[0]);
    $err = Users::setPassword((int)$u['id'], askNewPassword($stdinPw));
    if ($err !== null) fail($err);
    $n = isset($opts['keep-tokens']) ? 0 : AppPasswords::revokeAll((int)$u['id']);
    echo "Passwort von {$u['username']} geändert; offene Web-Sitzungen sind beendet"
       . ($n > 0 ? ", $n App-Passwort/-Passwörter widerrufen" : '') . ".\n";
    break;

case 'user:revoke-tokens':
    if (count($pos) !== 1) { usage(); exit(1); }
    $u = userOrFail($pos[0]);
    echo AppPasswords::revokeAll((int)$u['id']) . " App-Passwort/-Passwörter von {$u['username']} widerrufen.\n";
    break;

case 'user:admin':
case 'user:quota':
case 'user:disable':
case 'user:enable':
    $need = ($cmd === 'user:admin' || $cmd === 'user:quota') ? 2 : 1;
    if (count($pos) !== $need) { usage(); exit(1); }
    $u      = userOrFail($pos[0]);
    $quota  = $u['quota_bytes'] === null ? null : (int)$u['quota_bytes'];
    $admin  = (bool)(int)$u['is_admin'];
    $active = (bool)(int)$u['active'];
    if ($cmd === 'user:admin') {
        if (!in_array($pos[1], ['on', 'off'], true)) fail('Erwartet: on oder off');
        $admin = $pos[1] === 'on';
    } elseif ($cmd === 'user:quota') {
        $quota = Users::parseSize($pos[1]);
        if ($quota === false) fail('Ungültiges Kontingent: ' . $pos[1]);
    } else {
        $active = $cmd === 'user:enable';
    }
    $err = Users::updateAdmin((int)$u['id'], $quota, $admin, $active);
    if ($err !== null) fail($err);
    echo "Benutzer {$u['username']} geändert.\n";
    break;

case 'user:longnames':
    if (count($pos) !== 2) { usage(); exit(1); }
    $u = userOrFail($pos[0]);
    if (!in_array($pos[1], ['on', 'off'], true)) fail('Erwartet: on oder off');
    $err = Users::setLongNames((int)$u['id'], $pos[1] === 'on');
    if ($err !== null) fail($err);
    echo "Benutzer {$u['username']}: " . ($pos[1] === 'on' ? 'lange Dateinamen erlaubt' : 'Amiga-Modus') . ".\n";
    break;

case 'user:delete':
    if (count($pos) !== 1) { usage(); exit(1); }
    $u = userOrFail($pos[0]);
    if (!isset($opts['yes'])) {
        fwrite(STDOUT, "Benutzer {$u['username']} und ALLE seine Dateien löschen? Zur Bestätigung den Namen eintippen: ");
        if (trim((string)fgets(STDIN)) !== $u['username']) fail('Abgebrochen.');
    }
    $err = Users::delete((int)$u['id']);
    if ($err !== null) fail($err);
    echo "Benutzer {$u['username']} gelöscht.\n";
    break;

default:
    usage();
    exit($cmd === '' || $cmd === 'help' ? 0 : 1);
}
