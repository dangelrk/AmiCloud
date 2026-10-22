<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
if (!defined('AMICLOUD')) { http_response_code(403); exit; }

/* Persoenliche Einstellungen: Profil, Passwort, Geraete/App-Passwoerter,
 * Speicher. $newToken ist ein gerade angelegtes App-Passwort, das genau
 * einmal angezeigt wird. */
function renderSettings(?array $newToken): string
{
    $u      = Auth::$user;
    $quota  = Users::quotaInfo($u);
    $tokens = AppPasswords::listFor(Auth::$user_id);
    $server = 'https://' . ($_SERVER['HTTP_HOST'] ?? 'amicloud.example.com');
    ob_start();
?>
<h1 class="view-title"><?= icon('gear') ?>Persönliche Einstellungen</h1>

<div class="settings">
  <section class="card">
    <h2>Profil</h2>
    <form method="post" action="/web.php" class="form-grid">
      <?= csrfField() ?><input type="hidden" name="do" value="profile">
      <label for="pUser">Benutzername</label>
      <input id="pUser" type="text" value="<?= h(Auth::$username) ?>" disabled>
      <label for="pName">Anzeigename</label>
      <input id="pName" type="text" name="display_name" maxlength="128" value="<?= h($u['display_name']) ?>" placeholder="<?= h(Auth::$username) ?>">
      <label for="pMail">E-Mail</label>
      <input id="pMail" type="email" name="email" maxlength="255" value="<?= h($u['email']) ?>">
      <div class="form-actions"><button type="submit" class="btn btn-primary">Speichern</button></div>
    </form>
  </section>

  <section class="card">
    <h2>Passwort</h2>
    <form method="post" action="/web.php" class="form-grid">
      <?= csrfField() ?><input type="hidden" name="do" value="password">
      <label for="pwOld">Aktuelles Passwort</label>
      <input id="pwOld" type="password" name="current" autocomplete="current-password" required>
      <label for="pwNew">Neues Passwort</label>
      <input id="pwNew" type="password" name="new" autocomplete="new-password" minlength="8" required>
      <label for="pwNew2">Wiederholen</label>
      <input id="pwNew2" type="password" name="new2" autocomplete="new-password" minlength="8" required>
      <p class="form-hint">Andere angemeldete Browser werden abgemeldet. App-Passwörter bleiben gültig.</p>
      <div class="form-actions"><button type="submit" class="btn btn-primary">Passwort ändern</button></div>
    </form>
  </section>

  <section class="card card-wide" id="devices">
    <h2>Geräte &amp; App-Passwörter</h2>
    <p class="form-hint">Für jeden Amiga oder PC ein eigenes Passwort anlegen und in <code>amicloud.cfg</code>
       bzw. AmiCloudPrefs statt des Kontopassworts eintragen. Geht ein Gerät verloren, widerrufen Sie nur dessen Passwort.</p>

    <?php if ($newToken !== null): ?>
    <div class="newtoken">
      <strong>Neues App-Passwort für „<?= h($newToken['name']) ?>“</strong>
      <p>Es wird nur jetzt angezeigt. Für <code>amicloud.cfg</code>:</p>
      <pre data-copy-src>server=<?= h($server) . "\n" ?>user=<?= h(Auth::$username) . "\n" ?>pass=<?= h($newToken['token']) ?></pre>
      <button type="button" class="btn" data-copy><?= icon('copy') ?>Kopieren</button>
    </div>
    <?php endif; ?>

    <form method="post" action="/web.php" class="inline-form">
      <?= csrfField() ?><input type="hidden" name="do" value="apppw_create">
      <input type="text" name="name" maxlength="64" placeholder="Gerätename, z. B. A1200 CLOUD:" required aria-label="Gerätename">
      <button type="submit" class="btn btn-primary"><?= icon('key') ?>App-Passwort erstellen</button>
    </form>

    <?php if ($tokens !== []): ?>
    <div class="table-scroll">
    <table class="datatable">
      <thead><tr><th>Gerät</th><th>Erstellt</th><th>Zuletzt benutzt</th><th></th></tr></thead>
      <tbody>
      <?php foreach ($tokens as $t): ?>
        <tr>
          <td><?= icon('key') ?> <?= h($t['name']) ?></td>
          <td><?= h(date('d.m.Y', strtotime($t['created_at']))) ?></td>
          <td><?= $t['last_used_at'] ? h(relTime(strtotime($t['last_used_at']))) : 'nie' ?></td>
          <td class="col-right">
            <form method="post" action="/web.php" data-confirm="App-Passwort „<?= h($t['name']) ?>“ widerrufen? Das Gerät kann sich danach nicht mehr anmelden.">
              <?= csrfField() ?><input type="hidden" name="do" value="apppw_revoke"><input type="hidden" name="id" value="<?= (int)$t['id'] ?>">
              <button type="submit" class="btn btn-danger-ghost">Widerrufen</button>
            </form>
          </td>
        </tr>
      <?php endforeach; ?>
      </tbody>
    </table>
    </div>
    <?php endif; ?>
  </section>

  <section class="card">
    <h2>Speicherplatz</h2>
    <?php $pct = $quota['total'] > 0 ? min(100, (int)round($quota['used'] * 100 / $quota['total'])) : 0; ?>
    <div class="quota quota-big">
      <div class="quota-bar"><span style="width: <?= $pct ?>%"<?= $pct >= 90 ? ' class="full"' : '' ?>></span></div>
      <p><?= h(fmtSize($quota['used'])) ?> belegt ·
        <?= $quota['unlimited'] ? 'unbegrenztes Kontingent (' . h(fmtSize($quota['free'])) . ' frei auf dem Server)'
                                : h(fmtSize($quota['free'])) . ' von ' . h(fmtSize($quota['total'])) . ' frei' ?></p>
    </div>
  </section>

  <section class="card">
    <h2>Clients einrichten</h2>
    <p class="form-hint">Server-Adresse für AmiCloudPrefs, <code>amicloud</code>, die Shell und den Commander:</p>
    <pre data-copy-src><?= h($server) ?></pre>
    <button type="button" class="btn" data-copy><?= icon('copy') ?>Kopieren</button>
  </section>
</div>
<?php
    return (string)ob_get_clean();
}

function renderShares(): string
{
    $shares = Shares::listFor(Auth::$user_id);
    ob_start();
?>
<h1 class="view-title"><?= icon('share') ?>Freigaben</h1>
<p class="view-sub">Öffentliche Links auf Ihre Dateien und Ordner. Wer den Link kennt, kann herunterladen – ohne Konto.</p>
<?php if ($shares === []): ?>
  <div class="empty card"><?= icon('share', 'ic empty-ic') ?><strong>Noch keine Links</strong>
    <span>In der Dateiliste über „…“ → „Teilen“ einen Link erstellen.</span></div>
<?php else: ?>
<div class="table-scroll">
<table class="datatable">
  <thead><tr><th>Datei/Ordner</th><th>Link</th><th>Schutz</th><th>Läuft ab</th><th>Downloads</th><th></th></tr></thead>
  <tbody>
  <?php foreach ($shares as $s):
      $p = '/' . $s['path']; ?>
    <tr>
      <td><a href="<?= h(webUrl(['path' => parentPath($p)])) ?>"><?= h($p) ?></a></td>
      <td class="linkcell"><code data-copy-src><?= h(shareUrl($s['token'])) ?></code>
        <button type="button" class="icon-btn" data-copy aria-label="Link kopieren"><?= icon('copy') ?></button></td>
      <td><?= $s['has_pass'] ? icon('lock') . ' Passwort' : '–' ?></td>
      <td><?= $s['expires_at'] ? h(date('d.m.Y', strtotime($s['expires_at']))) : 'nie' ?></td>
      <td><?= (int)$s['downloads'] ?></td>
      <td class="col-right">
        <form method="post" action="/web.php" data-confirm="Link auf <?= h($p) ?> löschen?">
          <?= csrfField() ?><input type="hidden" name="do" value="share_delete"><input type="hidden" name="id" value="<?= (int)$s['id'] ?>">
          <input type="hidden" name="back" value="<?= h(webUrl(['view' => 'shares'])) ?>">
          <button type="submit" class="btn btn-danger-ghost">Löschen</button>
        </form>
      </td>
    </tr>
  <?php endforeach; ?>
  </tbody>
</table>
</div>
<?php endif;
    return (string)ob_get_clean();
}
