<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
if (!defined('AMICLOUD')) { http_response_code(403); exit; }

/* Seitengeruest im ownCloud-Stil: Kopfleiste (Logo, Suche, Benutzermenue),
 * Navigation links mit Kontingent, Inhalt rechts. $body ist fertiges HTML
 * der jeweiligen Ansicht. */
function renderLayout(string $title, string $active, string $body, string $query = ''): void
{
    $u       = Auth::$user;
    $name    = Users::displayName($u);
    $quota   = Users::quotaInfo($u);
    $pct     = $quota['total'] > 0 ? min(100, (int)round($quota['used'] * 100 / $quota['total'])) : 0;
    $flashes = takeFlashes();
    $nav = [
        'files'  => ['folder', 'Alle Dateien', webUrl()],
        'recent' => ['clock', 'Zuletzt geändert', webUrl(['view' => 'recent'])],
        'shares' => ['share', 'Freigaben', webUrl(['view' => 'shares'])],
    ];
?><!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title><?= h($title) ?> – AmiCloud</title>
  <link rel="icon" href="/logo.svg" type="image/svg+xml">
  <link rel="stylesheet" href="<?= h(cssUrl()) ?>">
</head>
<body class="app">
<?= iconSprite() ?>
<header class="appbar">
  <button type="button" class="icon-btn nav-toggle" aria-label="Navigation" data-toggle-nav><?= icon('menu') ?></button>
  <a href="<?= h(webUrl()) ?>" class="appbar-brand">
    <img src="/logo.svg" alt="" width="30" height="28">
    <span><span class="c-ami">Ami</span><span class="c-cloud">Cloud</span></span>
  </a>
  <form class="appbar-search" method="get" action="/web.php" role="search">
    <input type="hidden" name="view" value="search">
    <?= icon('search') ?>
    <input type="search" name="q" value="<?= h($query) ?>" placeholder="Dateien durchsuchen …" aria-label="Dateien durchsuchen">
  </form>
  <div class="usermenu">
    <button type="button" class="avatar-btn" aria-haspopup="menu" aria-expanded="false" data-menu="usermenu">
      <span class="avatar"><?= h(initials($name)) ?></span>
      <span class="avatar-name"><?= h($name) ?></span>
    </button>
    <div class="menu" id="usermenu" role="menu" hidden>
      <div class="menu-head"><?= h($name) ?><small><?= h(Auth::$username) ?></small></div>
      <a role="menuitem" href="<?= h(webUrl(['view' => 'settings'])) ?>"><?= icon('gear') ?>Persönliche Einstellungen</a>
      <?php if (Auth::$is_admin): ?>
      <a role="menuitem" href="<?= h(webUrl(['view' => 'users'])) ?>"><?= icon('users') ?>Benutzer</a>
      <?php endif; ?>
      <form method="post" action="/web.php?do=logout">
        <?= csrfField() ?>
        <button type="submit" role="menuitem"><?= icon('logout') ?>Abmelden</button>
      </form>
    </div>
  </div>
</header>

<div class="shell">
  <nav class="sidebar" id="sidebar">
    <ul class="nav">
      <?php foreach ($nav as $key => [$ic, $label, $url]): ?>
      <li><a href="<?= h($url) ?>"<?= $active === $key ? ' class="active" aria-current="page"' : '' ?>><?= icon($ic) ?><?= h($label) ?></a></li>
      <?php endforeach; ?>
    </ul>
    <div class="nav-bottom">
      <div class="quota" title="<?= $quota['unlimited'] ? 'Unbegrenztes Kontingent; Gesamt = belegt + freier Plattenplatz' : '' ?>">
        <div class="quota-bar"><span style="width: <?= $pct ?>%"<?= $pct >= 90 ? ' class="full"' : '' ?>></span></div>
        <div class="quota-text">
          <?= h(fmtSize($quota['used'])) ?> von <?= $quota['unlimited'] ? 'unbegrenzt' : h(fmtSize($quota['total'])) ?> verwendet
        </div>
      </div>
      <ul class="nav">
        <li><a href="<?= h(webUrl(['view' => 'settings'])) ?>"<?= $active === 'settings' ? ' class="active"' : '' ?>><?= icon('gear') ?>Einstellungen</a></li>
        <?php if (Auth::$is_admin): ?>
        <li><a href="<?= h(webUrl(['view' => 'users'])) ?>"<?= $active === 'users' ? ' class="active"' : '' ?>><?= icon('users') ?>Benutzer</a></li>
        <?php endif; ?>
      </ul>
    </div>
  </nav>

  <main class="content">
    <?php foreach ($flashes as $f): ?>
    <div class="flash flash-<?= h($f['type']) ?>" role="status"><?= h($f['msg']) ?><button type="button" class="icon-btn" data-dismiss aria-label="Schließen"><?= icon('close') ?></button></div>
    <?php endforeach; ?>
    <?= $body ?>
  </main>
</div>

<footer class="appfooter">AmiCloud v<?= h(AMICLOUD_VERSION) ?></footer>
<script src="<?= h(jsUrl()) ?>"></script>
</body>
</html>
<?php
}

/* Login-Seite (ohne Navigation) */
function renderLogin(string $error): void
{
?><!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Anmelden – AmiCloud</title>
  <link rel="icon" href="/logo.svg" type="image/svg+xml">
  <link rel="stylesheet" href="<?= h(cssUrl()) ?>">
</head>
<body class="login-page">
  <div class="login-box">
    <a href="/" class="login-logo"><img src="/logo.svg" alt="" width="96" height="89"></a>
    <h1><span class="c-ami">Ami</span><span class="c-cloud">Cloud</span></h1>
    <?php if ($error !== ''): ?>
      <div class="msg-error"><?= h($error) ?></div>
    <?php endif; ?>
    <form method="post" action="/web.php?do=login">
      <?= csrfField() ?>
      <label>Benutzer
        <input type="text" name="username" autofocus autocomplete="username" autocapitalize="none" spellcheck="false">
      </label>
      <label>Passwort
        <input type="password" name="password" autocomplete="current-password">
      </label>
      <button type="submit">Anmelden</button>
    </form>
  </div>
</body>
</html>
<?php
}
