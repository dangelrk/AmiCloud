<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
if (!defined('AMICLOUD')) { http_response_code(403); exit; }

/* Kontingent-Auswahl: feste Stufen plus eigener Wert */
function quotaSelect(string $id, ?int $current): string
{
    $presets = ['' => 'Unbegrenzt', '1073741824' => '1 GB', '5368709120' => '5 GB',
                '10737418240' => '10 GB', '53687091200' => '50 GB'];
    $cur     = $current === null ? '' : (string)$current;
    $custom  = !array_key_exists($cur, $presets);
    $html    = '<select id="' . h($id) . '" name="quota_preset" data-quota-select>';
    foreach ($presets as $v => $label) {
        $html .= '<option value="' . h((string)$v) . '"' . (!$custom && $cur === (string)$v ? ' selected' : '') . '>' . h($label) . '</option>';
    }
    $html .= '<option value="custom"' . ($custom ? ' selected' : '') . '>Eigener Wert …</option></select>';
    $html .= '<input type="text" name="quota_custom" placeholder="z. B. 2G oder 500M" data-quota-custom'
           . ($custom ? ' value="' . h(fmtSize((int)$current)) . '"' : ' hidden') . '>';
    return $html;
}

function renderUsers(): string
{
    $users = Users::all();
    ob_start();
?>
<div class="view-head">
  <h1 class="view-title"><?= icon('users') ?>Benutzer</h1>
  <button type="button" class="btn btn-primary" data-open-dialog="dlgUserNew"><?= icon('plus') ?>Benutzer anlegen</button>
</div>

<div class="table-scroll">
<table class="datatable users">
  <thead><tr><th>Benutzer</th><th>E-Mail</th><th>Speicher</th><th>Rolle</th><th>Letzte Anmeldung</th><th></th></tr></thead>
  <tbody>
  <?php foreach ($users as $u):
      $used  = (int)$u['used_bytes'];
      $quota = $u['quota_bytes'] === null ? null : (int)$u['quota_bytes'];
      $pct   = $quota ? min(100, (int)round($used * 100 / max(1, $quota))) : 0;
      $self  = (int)$u['id'] === Auth::$user_id; ?>
    <tr class="<?= $u['active'] ? '' : 'inactive' ?>">
      <td>
        <div class="usercell">
          <span class="avatar"><?= h(initials($u['display_name'] !== '' ? $u['display_name'] : $u['username'])) ?></span>
          <span><strong><?= h($u['display_name'] !== '' ? $u['display_name'] : $u['username']) ?></strong>
            <small><?= h($u['username']) ?><?= $self ? ' · Sie' : '' ?></small></span>
        </div>
      </td>
      <td><?= h($u['email']) ?></td>
      <td class="col-quota">
        <?= h(fmtSize($used)) ?> / <?= $quota === null ? 'unbegrenzt' : h(fmtSize($quota)) ?>
        <?php if ($quota !== null): ?><div class="quota-bar small"><span style="width: <?= $pct ?>%"<?= $pct >= 90 ? ' class="full"' : '' ?>></span></div><?php endif; ?>
      </td>
      <td>
        <?php if ($u['is_admin']): ?><span class="pill pill-admin">Admin</span><?php endif; ?>
        <?php if (!$u['active']): ?><span class="pill pill-off">Deaktiviert</span><?php endif; ?>
      </td>
      <td><?= $u['last_login'] ? h(relTime(strtotime($u['last_login']))) : 'nie' ?></td>
      <td class="col-right nowrap">
        <button type="button" class="btn" data-edit-user='<?= h(json_encode([
            'id' => (int)$u['id'], 'username' => $u['username'], 'display_name' => $u['display_name'],
            'email' => $u['email'], 'quota' => $quota, 'is_admin' => (bool)$u['is_admin'],
            'active' => (bool)$u['active'], 'self' => $self,
        ], JSON_UNESCAPED_UNICODE)) ?>'><?= icon('edit') ?>Bearbeiten</button>
        <?php if (!$self): ?>
        <button type="button" class="btn btn-danger-ghost" data-delete-user='<?= h(json_encode(['id' => (int)$u['id'], 'username' => $u['username']])) ?>'>Löschen</button>
        <?php endif; ?>
      </td>
    </tr>
  <?php endforeach; ?>
  </tbody>
</table>
</div>

<dialog id="dlgUserNew" class="dlg dlg-wide">
  <form method="post" action="/web.php" class="form-grid">
    <?= csrfField() ?><input type="hidden" name="do" value="user_create">
    <h2>Benutzer anlegen</h2>
    <label for="nuName">Benutzername</label>
    <input id="nuName" name="username" required maxlength="64" pattern="[a-z0-9][a-z0-9._\-]{0,63}" autocapitalize="none" spellcheck="false"
           title="Kleinbuchstaben, Ziffern, Punkt, Bindestrich, Unterstrich">
    <label for="nuPass">Passwort</label>
    <input id="nuPass" name="password" type="password" required minlength="8" autocomplete="new-password">
    <label for="nuDisplay">Anzeigename</label>
    <input id="nuDisplay" name="display_name" maxlength="128">
    <label for="nuMail">E-Mail</label>
    <input id="nuMail" name="email" type="email" maxlength="255">
    <label for="nuQuota">Kontingent</label>
    <div class="row"><?= quotaSelect('nuQuota', null) ?></div>
    <span></span><label class="check"><input type="checkbox" name="is_admin" value="1"> Administrator</label>
    <div class="dlg-actions">
      <button type="button" class="btn" data-close-dialog>Abbrechen</button>
      <button type="submit" class="btn btn-primary">Anlegen</button>
    </div>
  </form>
</dialog>

<dialog id="dlgUserEdit" class="dlg dlg-wide">
  <form method="post" action="/web.php" class="form-grid">
    <?= csrfField() ?><input type="hidden" name="do" value="user_update"><input type="hidden" name="id">
    <h2>Benutzer <span data-username></span> bearbeiten</h2>
    <label for="euDisplay">Anzeigename</label>
    <input id="euDisplay" name="display_name" maxlength="128">
    <label for="euMail">E-Mail</label>
    <input id="euMail" name="email" type="email" maxlength="255">
    <label for="euQuota">Kontingent</label>
    <div class="row" data-quota-slot></div>
    <label for="euPass">Neues Passwort</label>
    <input id="euPass" name="password" type="password" minlength="8" autocomplete="new-password" placeholder="leer = unverändert">
    <span></span><label class="check"><input type="checkbox" name="is_admin" value="1"> Administrator</label>
    <span></span><label class="check"><input type="checkbox" name="active" value="1"> Aktiv (darf sich anmelden)</label>
    <div class="dlg-actions">
      <button type="button" class="btn" data-close-dialog>Abbrechen</button>
      <button type="submit" class="btn btn-primary">Speichern</button>
    </div>
  </form>
</dialog>
<template id="tplQuota"><?= quotaSelect('euQuota', null) ?></template>

<dialog id="dlgUserDelete" class="dlg">
  <form method="post" action="/web.php">
    <?= csrfField() ?><input type="hidden" name="do" value="user_delete"><input type="hidden" name="id">
    <h2>Benutzer löschen</h2>
    <p>Der Benutzer <strong data-username></strong> und <strong>alle seine Dateien, App-Passwörter und Links</strong>
       werden endgültig gelöscht. Zur Bestätigung den Benutzernamen eintippen:</p>
    <input name="confirm" required autocomplete="off" autocapitalize="none" spellcheck="false" aria-label="Benutzername zur Bestätigung">
    <div class="dlg-actions">
      <button type="button" class="btn" data-close-dialog>Abbrechen</button>
      <button type="submit" class="btn btn-danger">Endgültig löschen</button>
    </div>
  </form>
</dialog>
<?php
    return (string)ob_get_clean();
}
