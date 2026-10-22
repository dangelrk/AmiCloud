<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
define('AMICLOUD', 1);
require __DIR__ . '/config.php';

/* Oeffentliche Linkseite ohne Login: share.php?t=<token>[&p=<unterpfad>]
 * [&dl=1 | &zip=1]. Dateien gehen immer als Download raus (nie inline),
 * der Unterpfad bleibt per sanitizePath() im geteilten Ordner. */

$https = !empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off';
if (!$https && !(defined('AMICLOUD_ALLOW_HTTP') && AMICLOUD_ALLOW_HTTP)) {
    header('Location: https://' . $_SERVER['HTTP_HOST'] . $_SERVER['REQUEST_URI']);
    exit;
}
/* Eigener Cookie-Name: der Besuch kommt meist von einer fremden Seite
 * (Mail, Chat); mit SameSite=Strict wuerde PHP sonst eine neue Session
 * anlegen und dabei die web.php-Session des Eigentuemers ueberschreiben. */
session_name('AMISHARE');
session_set_cookie_params(['lifetime' => 0, 'path' => '/share.php', 'secure' => $https,
                           'httponly' => true, 'samesite' => 'Lax']);
session_start();

require __DIR__ . '/includes/Version.php';
require __DIR__ . '/includes/Response.php';
require __DIR__ . '/includes/DB.php';
require __DIR__ . '/includes/Throttle.php';
require __DIR__ . '/includes/Users.php';
require __DIR__ . '/includes/Shares.php';
require __DIR__ . '/includes/Auth.php';
require __DIR__ . '/includes/FileStore.php';
require __DIR__ . '/includes/Zip.php';
require __DIR__ . '/includes/web/helpers.php';

header('Referrer-Policy: no-referrer');
header('X-Robots-Tag: noindex, nofollow');
header('X-Content-Type-Options: nosniff');
header('X-Frame-Options: DENY');

function sharePage(string $title, string $body): void
{
?><!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="robots" content="noindex, nofollow">
  <title><?= h($title) ?> – AmiCloud</title>
  <link rel="icon" href="/logo.svg" type="image/svg+xml">
  <link rel="stylesheet" href="<?= h(cssUrl()) ?>">
</head>
<body class="sharepage">
<?= iconSprite() ?>
<header class="share-head">
  <a href="/" class="appbar-brand"><img src="/logo.svg" alt="" width="30" height="28"><span><span class="c-ami">Ami</span><span class="c-cloud">Cloud</span></span></a>
</header>
<main class="share-main"><?= $body ?></main>
</body>
</html>
<?php
    exit;
}

function notFound(): void
{
    http_response_code(404);
    sharePage('Link nicht gefunden', '<div class="card share-card empty">' . icon('share', 'ic empty-ic')
        . '<strong>Dieser Link ist ungültig oder abgelaufen.</strong>'
        . '<span>Bitte bei der Person nachfragen, die ihn geteilt hat.</span></div>');
}

$token = (string)($_GET['t'] ?? '');
$share = Shares::byToken($token);
if ($share === null) {
    notFound();
}
$owner = Users::find((int)$share['user_id']);
if ($owner === null) {
    notFound();
}
Auth::setUser($owner);
$self = '/share.php?t=' . $token;

/* ── Passwortschutz ──────────────────────────────────────────────────── */
if ($share['pass_hash'] !== null && empty($_SESSION['unlocked'][$token])) {
    $error = '';
    if (empty($_SESSION['csrf'])) {
        $_SESSION['csrf'] = bin2hex(random_bytes(16));
    }
    if ($_SERVER['REQUEST_METHOD'] === 'POST') {
        $scope = 'share:' . $token;
        $fails = Throttle::failures($scope);
        if (!hash_equals($_SESSION['csrf'], (string)($_POST['csrf'] ?? ''))) {
            $error = 'Die Sitzung ist abgelaufen. Bitte erneut versuchen.';
        } elseif (Throttle::blocked($fails, 10)) {
            $error = 'Zu viele Fehlversuche. Bitte in 15 Minuten erneut versuchen.';
        } elseif (password_verify((string)($_POST['password'] ?? ''), $share['pass_hash'])) {
            session_regenerate_id(true);
            $_SESSION['unlocked'][$token] = true;
            if ($fails[0] > 0) {
                Throttle::clear($scope);
            }
            header('Location: ' . $self);
            exit;
        } else {
            Throttle::fail($scope);
            $error = 'Falsches Passwort.';
        }
    }
    ob_start(); ?>
    <form method="post" action="<?= h($self) ?>" class="card share-card share-login">
      <?= icon('lock', 'ic empty-ic') ?>
      <strong>Dieser Link ist passwortgeschützt</strong>
      <?php if ($error !== ''): ?><div class="msg-error"><?= h($error) ?></div><?php endif; ?>
      <input type="hidden" name="csrf" value="<?= h($_SESSION['csrf']) ?>">
      <input type="password" name="password" placeholder="Passwort" autofocus required aria-label="Passwort">
      <button type="submit" class="btn btn-primary">Öffnen</button>
    </form>
    <?php sharePage('Passwort erforderlich', (string)ob_get_clean());
}

/* ── Inhalt ──────────────────────────────────────────────────────────── */
$base = '/' . $share['path'];
$abs  = storagePath($base);
if (!file_exists($abs)) {
    notFound();
}
$from = 'Geteilt von ' . Users::displayName($owner);

if (is_file($abs)) {
    if (isset($_GET['dl'])) {
        Shares::countDownload((int)$share['id']);
        FileStore::sendAttachment($abs, basename($abs));
    }
    $name = basename($abs);
    ob_start(); ?>
    <div class="card share-card share-file">
      <span class="kind kind-big kind-<?= fileKind($name, false) ?>"><?= icon(fileKind($name, false)) ?></span>
      <h1><?= h($name) ?></h1>
      <p class="muted"><?= h(fmtSize((int)filesize($abs))) ?> · <?= h($from) ?></p>
      <a class="btn btn-primary" href="<?= h($self . '&dl=1') ?>"><?= icon('download') ?>Herunterladen</a>
    </div>
    <?php sharePage($name, (string)ob_get_clean());
}

/* Ordner-Freigabe: Unterpfad relativ zum geteilten Ordner */
$sub = sanitizePath((string)($_GET['p'] ?? '/'));
$cur = $sub === '/' ? $base : $base . $sub;
$curAbs = storagePath($cur);

if (isset($_GET['dl']) && is_file($curAbs)) {
    Shares::countDownload((int)$share['id']);
    FileStore::sendAttachment($curAbs, basename($curAbs));
}
if (!is_dir($curAbs)) {
    notFound();
}
if (isset($_GET['zip'])) {
    Shares::countDownload((int)$share['id']);
    $err = Zip::send([$cur], basename($curAbs) . '.zip');
    sharePage('Fehler', '<div class="card share-card"><p class="msg-error">' . h($err) . '</p></div>');
}

$entries = FileStore::listRaw($cur);
usort($entries, function (array $a, array $b): int {
    if (($a['type'] === 'dir') !== ($b['type'] === 'dir')) {
        return $a['type'] === 'dir' ? -1 : 1;
    }
    return strnatcasecmp($a['name'], $b['name']);
});
$rootName = basename($abs);

ob_start(); ?>
<div class="share-folder">
  <div class="share-folder-head">
    <div>
      <h1><?= icon('folder') ?><?= h($rootName) ?></h1>
      <p class="muted"><?= h($from) ?></p>
    </div>
    <a class="btn btn-primary" href="<?= h($self . '&zip=1' . ($sub !== '/' ? '&p=' . rawurlencode($sub) : '')) ?>"><?= icon('download') ?>Alles als ZIP</a>
  </div>
  <nav class="breadcrumb">
    <a href="<?= h($self) ?>"><?= h($rootName) ?></a>
    <?php $cumul = '';
    foreach (array_filter(explode('/', $sub), 'strlen') as $part):
        $cumul .= '/' . $part; ?>
      <?= icon('chevron', 'ic crumb-sep') ?><a href="<?= h($self . '&p=' . rawurlencode($cumul)) ?>"><?= h($part) ?></a>
    <?php endforeach; ?>
  </nav>
  <table class="filetable">
    <thead><tr><th class="col-name">Name</th><th class="col-size">Größe</th><th class="col-mod">Geändert</th></tr></thead>
    <tbody>
    <?php foreach ($entries as $e):
        $isDir = $e['type'] === 'dir';
        $p     = rtrim($sub, '/') . '/' . $e['name'];
        $href  = $self . '&p=' . rawurlencode($p) . ($isDir ? '' : '&dl=1'); ?>
      <tr>
        <td class="col-name"><a class="name" href="<?= h($href) ?>">
          <span class="kind kind-<?= fileKind($e['name'], $isDir) ?>"><?= icon(fileKind($e['name'], $isDir)) ?></span>
          <span class="name-text"><?= h($e['name']) ?></span></a></td>
        <td class="col-size"><?= $isDir ? '' : h(fmtSize($e['size'])) ?></td>
        <td class="col-mod" title="<?= h(date('d.m.Y H:i', $e['mtime'])) ?>"><?= h(relTime($e['mtime'])) ?></td>
      </tr>
    <?php endforeach; ?>
    <?php if ($entries === []): ?>
      <tr class="empty-row"><td colspan="3"><div class="empty"><strong>Dieser Ordner ist leer</strong></div></td></tr>
    <?php endif; ?>
    </tbody>
  </table>
</div>
<?php sharePage($rootName, (string)ob_get_clean());
