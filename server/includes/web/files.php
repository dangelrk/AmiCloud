<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
if (!defined('AMICLOUD')) { http_response_code(403); exit; }

function breadcrumbHtml(string $path): string
{
    $html  = '<a href="' . h(webUrl()) . '" class="crumb-home" title="Alle Dateien">' . icon('home') . '</a>';
    $cumul = '';
    foreach (array_filter(explode('/', $path), 'strlen') as $part) {
        $cumul .= '/' . $part;
        $html  .= icon('chevron', 'ic crumb-sep')
               . '<a href="' . h(webUrl(['path' => $cumul])) . '">' . h($part) . '</a>';
    }
    return $html;
}

/* Klickbarer Spaltenkopf; dreht die Richtung um, wenn schon danach
 * sortiert wird. "Ordner zuerst" bleibt beim Umsortieren erhalten. */
function sortLinkHtml(string $path, string $column, string $label, array $sort): string
{
    $newDir = ($sort['by'] === $column && $sort['dir'] === 'asc') ? 'desc' : 'asc';
    $url    = webUrl(['path' => $path, 'sort' => $column, 'dir' => $newDir,
                      'foldersfirst' => $sort['foldersFirst'] ? '1' : '0']);
    $arrow  = $sort['by'] === $column ? '<span class="sort-arrow">' . ($sort['dir'] === 'asc' ? '▲' : '▼') . '</span>' : '';
    return '<a href="' . h($url) . '" class="sort-link">' . h($label) . $arrow . '</a>';
}

/* $mode: files | recent | search. $entries sind normalisiert:
 * name, path, type, size, mtime, comment, (bei Ordnern) counts. */
function renderFiles(string $mode, string $path, array $entries, array $sort, string $query = ''): string
{
    $shared   = Shares::sharedPaths(Auth::$user_id);
    $existing = [];
    foreach ($entries as $e) {
        $existing[] = $e['name'];
    }
    ob_start();
?>
<div class="files" data-dir="<?= h($path) ?>" data-mode="<?= h($mode) ?>"
     data-existing="<?= h(json_encode($existing, JSON_UNESCAPED_UNICODE)) ?>">
  <div class="toolbar" data-toolbar>
    <?php if ($mode === 'files'): ?>
    <nav class="breadcrumb" aria-label="Pfad"><?= breadcrumbHtml($path) ?></nav>
    <div class="newmenu">
      <button type="button" class="btn btn-primary" data-menu="newmenu" aria-haspopup="menu"><?= icon('plus') ?>Neu</button>
      <div class="menu" id="newmenu" role="menu" hidden>
        <button type="button" role="menuitem" data-action="mkdir"><?= icon('folder') ?>Neuer Ordner</button>
        <button type="button" role="menuitem" data-action="pick-files"><?= icon('upload') ?>Datei hochladen</button>
        <button type="button" role="menuitem" data-action="pick-folder"><?= icon('upload') ?>Ordner hochladen</button>
      </div>
    </div>
    <?php elseif ($mode === 'recent'): ?>
    <h1 class="view-title"><?= icon('clock') ?>Zuletzt geändert</h1>
    <?php else: ?>
    <h1 class="view-title"><?= icon('search') ?>Suche nach „<?= h($query) ?>“</h1>
    <?php endif; ?>
  </div>

  <div class="selectionbar" data-selectionbar hidden>
    <span data-selcount></span>
    <button type="button" class="btn" data-action="zip"><?= icon('download') ?>Herunterladen</button>
    <button type="button" class="btn" data-action="move"><?= icon('move') ?>Verschieben</button>
    <button type="button" class="btn btn-danger" data-action="delete"><?= icon('trash') ?>Löschen</button>
    <button type="button" class="icon-btn" data-action="clear-selection" aria-label="Auswahl aufheben"><?= icon('close') ?></button>
  </div>

  <div class="dropzone-wrap">
  <table class="filetable">
    <thead>
      <tr>
        <th class="col-check"><input type="checkbox" data-check-all aria-label="Alle auswählen"></th>
        <?php if ($mode === 'files'): ?>
        <th class="col-name"><?= sortLinkHtml($path, 'name', 'Name', $sort) ?></th>
        <th class="col-act"></th>
        <th class="col-size"><?= sortLinkHtml($path, 'size', 'Größe', $sort) ?></th>
        <th class="col-mod"><?= sortLinkHtml($path, 'modified', 'Geändert', $sort) ?></th>
        <?php else: ?>
        <th class="col-name">Name</th><th class="col-act"></th><th class="col-size">Größe</th><th class="col-mod">Geändert</th>
        <?php endif; ?>
      </tr>
    </thead>
    <tbody>
    <?php foreach ($entries as $e):
        $isDir = $e['type'] === 'dir';
        $c     = $e['counts'];
    ?>
      <tr data-path="<?= h($e['path']) ?>" data-name="<?= h($e['name']) ?>" data-type="<?= h($e['type']) ?>"
          data-files="<?= $c['files'] ?>" data-dirs="<?= $c['dirs'] ?>" data-bytes="<?= $c['bytes'] ?>">
        <td class="col-check"><input type="checkbox" class="sel" value="<?= h($e['path']) ?>" aria-label="<?= h($e['name']) ?> auswählen"></td>
        <td class="col-name">
          <a class="name" href="<?= h($isDir ? webUrl(['path' => $e['path']]) : webUrl(['do' => 'download', 'path' => $e['path']])) ?>">
            <span class="kind kind-<?= fileKind($e['name'], $isDir) ?>"><?= icon(fileKind($e['name'], $isDir)) ?></span>
            <span class="name-text"><?= h($e['name']) ?></span>
          </a>
          <?php if (isset($shared[$e['path']])): ?>
          <span class="badge-shared" title="Öffentlicher Link vorhanden"><?= icon('share') ?></span>
          <?php endif; ?>
          <?php if ($mode !== 'files'): ?>
          <span class="name-dir"><?= h(parentPath($e['path'])) ?></span>
          <?php endif; ?>
          <?php if (($e['comment'] ?? '') !== ''): ?>
          <span class="name-comment" title="Amiga-Kommentar"><?= h($e['comment']) ?></span>
          <?php endif; ?>
        </td>
        <td class="col-act"><button type="button" class="icon-btn" data-row-menu aria-label="Aktionen für <?= h($e['name']) ?>"><?= icon('more') ?></button></td>
        <td class="col-size"><?= $isDir ? h(fmtSize($c['bytes'])) : h(fmtSize($e['size'])) ?></td>
        <td class="col-mod" title="<?= h(date('d.m.Y H:i', $e['mtime'])) ?>"><?= h(relTime($e['mtime'])) ?></td>
      </tr>
    <?php endforeach; ?>
    <?php if ($entries === []): ?>
      <tr class="empty-row"><td colspan="5">
        <div class="empty">
          <?php if ($mode === 'files'): ?>
            <?= icon('upload', 'ic empty-ic') ?><strong>Hier ist noch nichts</strong>
            <span>Dateien hierher ziehen oder über „Neu“ hochladen.</span>
          <?php elseif ($mode === 'search'): ?>
            <?= icon('search', 'ic empty-ic') ?><strong>Keine Treffer</strong>
            <span>Teilwort oder Joker wie <code>*.lha</code> probieren.</span>
          <?php else: ?>
            <?= icon('clock', 'ic empty-ic') ?><strong>Noch keine Dateien</strong>
          <?php endif; ?>
        </div>
      </td></tr>
    <?php endif; ?>
    </tbody>
  </table>
  <?php if ($mode === 'files'): ?>
  <div class="dropzone" data-dropzone hidden><?= icon('upload', 'ic dropzone-ic') ?>Zum Hochladen loslassen</div>
  <?php endif; ?>
  </div>

  <?php if ($mode === 'files'): ?>
  <label class="foldersfirst-toggle">
    <input type="checkbox" data-foldersfirst <?= $sort['foldersFirst'] ? 'checked' : '' ?>
           data-url="<?= h(webUrl(['path' => $path, 'sort' => $sort['by'], 'dir' => $sort['dir']])) ?>">
    Ordner zuerst
  </label>
  <input type="file" id="fileInput" multiple hidden>
  <input type="file" id="folderInput" webkitdirectory hidden>
  <?php endif; ?>
</div>

<!-- Zeilenmenue (eins fuer alle Zeilen, per JS positioniert) -->
<div class="menu rowmenu" id="rowmenu" role="menu" hidden>
  <button type="button" role="menuitem" data-row-action="details"><?= icon('info') ?>Details</button>
  <button type="button" role="menuitem" data-row-action="download"><?= icon('download') ?>Herunterladen</button>
  <button type="button" role="menuitem" data-row-action="rename"><?= icon('edit') ?>Umbenennen</button>
  <button type="button" role="menuitem" data-row-action="move"><?= icon('move') ?>Verschieben</button>
  <button type="button" role="menuitem" data-row-action="share"><?= icon('share') ?>Teilen</button>
  <button type="button" role="menuitem" data-row-action="delete" class="danger"><?= icon('trash') ?>Löschen</button>
</div>

<!-- Eine Formularvorlage fuer alle POST-Aktionen; JS fuellt sie -->
<form method="post" action="/web.php" id="actForm" hidden>
  <?= csrfField() ?>
  <input type="hidden" name="do">
  <input type="hidden" name="back" value="<?= h($_SERVER['REQUEST_URI'] ?? '/web.php') ?>">
</form>

<dialog id="dlgName" class="dlg">
  <form method="dialog">
    <h2 data-title></h2>
    <label><span data-label></span><input type="text" name="name" required maxlength="255" autocomplete="off"></label>
    <p class="dlg-error" data-error hidden></p>
    <div class="dlg-actions">
      <button value="cancel" class="btn" formnovalidate>Abbrechen</button>
      <button value="ok" class="btn btn-primary" data-ok></button>
    </div>
  </form>
</dialog>

<dialog id="dlgConfirm" class="dlg">
  <form method="dialog">
    <h2 data-title></h2>
    <p data-text></p>
    <div class="dlg-actions">
      <button value="cancel" class="btn">Abbrechen</button>
      <button value="ok" class="btn btn-danger" data-ok></button>
    </div>
  </form>
</dialog>

<dialog id="dlgMove" class="dlg dlg-wide">
  <form method="dialog">
    <h2 data-title>Verschieben nach …</h2>
    <div class="tree" data-tree></div>
    <p class="dlg-hint">Ziel: <strong data-target>/</strong></p>
    <div class="dlg-actions">
      <button value="cancel" class="btn">Abbrechen</button>
      <button value="ok" class="btn btn-primary">Hierher verschieben</button>
    </div>
  </form>
</dialog>

<dialog id="dlgConflict" class="dlg">
  <form method="dialog">
    <h2>Dateien existieren bereits</h2>
    <p data-text></p>
    <ul class="conflict-list" data-list></ul>
    <div class="dlg-actions">
      <button value="cancel" class="btn">Abbrechen</button>
      <button value="skip" class="btn">Überspringen</button>
      <button value="replace" class="btn btn-primary">Ersetzen</button>
    </div>
  </form>
</dialog>

<aside class="details" id="details" hidden aria-label="Details">
  <div class="details-head">
    <span class="kind" data-d-icon></span>
    <div class="details-title"><strong data-d-name></strong><small data-d-sub></small></div>
    <button type="button" class="icon-btn" data-close-details aria-label="Details schließen"><?= icon('close') ?></button>
  </div>
  <div class="details-tabs" role="tablist">
    <button type="button" role="tab" data-tab="info" aria-selected="true">Details</button>
    <button type="button" role="tab" data-tab="share" aria-selected="false">Teilen</button>
  </div>
  <div class="details-body" data-panel="info">
    <dl class="meta" data-d-meta></dl>
    <form class="comment-form" data-comment-form>
      <label for="dComment">Amiga-Kommentar</label>
      <div class="row"><input type="text" id="dComment" maxlength="79" placeholder="kein Kommentar"><button type="submit" class="btn">Speichern</button></div>
    </form>
  </div>
  <div class="details-body" data-panel="share" hidden>
    <ul class="sharelist" data-d-shares></ul>
    <form class="share-form" data-share-form>
      <h3>Neuer öffentlicher Link</h3>
      <label for="sPass">Passwort (optional)</label>
      <input type="text" id="sPass" autocomplete="off" maxlength="200">
      <label for="sExp">Ablaufdatum (optional)</label>
      <input type="date" id="sExp">
      <button type="submit" class="btn btn-primary"><?= icon('share') ?>Link erstellen</button>
    </form>
  </div>
</aside>

<div class="uploads" data-uploads hidden>
  <div class="uploads-head"><strong data-up-title>Hochladen …</strong><span data-up-count></span></div>
  <div class="progress"><span data-up-bar></span></div>
  <p class="uploads-err" data-up-err hidden></p>
</div>
<?php
    return (string)ob_get_clean();
}
