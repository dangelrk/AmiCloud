<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
define('AMICLOUD', 1);

/* Startseite. Die Clients sprechen ausschliesslich api.php an, die
 * Weboberflaeche liegt unter web.php -- diese Seite braucht weder
 * config.php noch die Datenbank. */
require __DIR__ . '/includes/Version.php';

function h(string $s): string
{
    return htmlspecialchars($s, ENT_QUOTES, 'UTF-8');
}

$serverUrl = 'https://' . ($_SERVER['HTTP_HOST'] ?? 'amicloud.example.com');

/* Angemeldet? Nur nachsehen, wenn schon ein Session-Cookie existiert --
 * ohne DB und ohne config.php (web.php prueft die Session ohnehin). */
$loggedIn = false;
if (isset($_COOKIE[session_name()])) {
    session_start(['read_and_close' => true]);
    $loggedIn = isset($_SESSION['user_id']);
}
?><!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AmiCloud v<?= AMICLOUD_VERSION ?></title>
  <meta name="description" content="AmiCloud – eigener Cloud-Speicher für den Amiga und den PC">
  <link rel="icon" href="/logo.svg" type="image/svg+xml">
  <link rel="stylesheet" href="/web.css?v=<?= filemtime(__DIR__ . '/web.css') ?>">
</head>
<body class="landing">
  <div class="copper" aria-hidden="true"></div>

  <div class="landing-bg" aria-hidden="true"><?php readfile(__DIR__ . '/logo.svg'); ?></div>

  <header class="hero">
    <h1 class="hero-title"><span class="c-ami">Ami</span><span class="c-cloud">Cloud</span></h1>
    <p class="hero-tagline">Der eigene Cloud-Speicher für den Amiga – und den PC.</p>
    <div class="hero-actions">
      <a href="/web.php" class="btn-hero"><?= $loggedIn ? 'Zu meinen Dateien' : 'Anmelden' ?></a>
      <a href="https://github.com/dangelrk/AmiCloud" class="btn-hero-alt">Projekt auf GitHub</a>
    </div>
  </header>

  <main class="features">
    <section class="feature">
      <h2><code>CLOUD:</code></h2>
      <p>Der DOS-Handler bindet die Cloud als ganz normales Laufwerk ein –
         mit Protection-Bits, Dateikommentaren und Datumsangaben.</p>
    </section>
    <section class="feature">
      <h2>amicloud &amp; Shell</h2>
      <p>Einzelbefehle wie <code>list</code>, <code>get</code>, <code>put</code>
         oder eine interaktive Sitzung mit <code>cd</code>, <code>ls</code> und Mustern.</p>
    </section>
    <section class="feature">
      <h2>Commander</h2>
      <p>Dateimanager mit zwei Panels, jedes lokal oder Cloud – MUI auf dem
         Amiga, Qt auf macOS, Linux und Windows.</p>
    </section>
    <section class="feature">
      <h2>cloudsync</h2>
      <p>Zwei-Wege-Abgleich eines lokalen Ordners mit einem Cloud-Ordner,
         mit MD5-Prüfung.</p>
    </section>

    <section class="setup">
      <h2>Einrichten</h2>
      <p>In AmiCloudPrefs bzw. <code>amicloud.cfg</code> diesen Server eintragen:</p>
      <pre>server=<?= h($serverUrl) . "\n" ?>
user=…
pass=…</pre>
    </section>
  </main>

  <footer class="landing-footer">
    AmiCloud v<?= AMICLOUD_VERSION ?> · GPL-3.0 · AmigaOS 3.x, bsdsocket + AmiSSL
  </footer>
</body>
</html>
