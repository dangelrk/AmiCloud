<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
session_set_cookie_params([
    'lifetime' => 0,
    'path'     => '/',
    'secure'   => true,
    'httponly' => true,
    'samesite' => 'Strict',
]);
session_start();
define('AMICLOUD', 1);

require __DIR__ . '/config.php';
require __DIR__ . '/includes/Version.php';
require __DIR__ . '/includes/Response.php';
require __DIR__ . '/includes/DB.php';
require __DIR__ . '/includes/Auth.php';
require __DIR__ . '/includes/FileStore.php';

/* ── Helpers ─────────────────────────────────────────────────────────── */

function h(string $s): string
{
    return htmlspecialchars($s, ENT_QUOTES, 'UTF-8');
}

function fmtSize(int $bytes): string
{
    if ($bytes < 1024)       return $bytes . ' B';
    if ($bytes < 1048576)    return round($bytes / 1024, 1) . ' KB';
    if ($bytes < 1073741824) return round($bytes / 1048576, 1) . ' MB';
    return round($bytes / 1073741824, 1) . ' GB';
}

function breadcrumbHtml(string $path): string
{
    $html  = '<a href="/web.php?path=/">/</a>';
    $parts = array_filter(explode('/', $path));
    $cumul = '';
    foreach ($parts as $part) {
        $cumul .= '/' . $part;
        $html  .= ' <span class="sep">›</span> '
               . '<a href="/web.php?path=' . urlencode($cumul) . '">' . h($part) . '</a>';
    }
    return $html;
}

/* Klickbarer Spaltenkopf ("Name"/"Größe"/"Geändert"): sortiert bei Klick
 * nach dieser Spalte, oder dreht die Richtung um, wenn schon danach
 * sortiert wird. foldersFirst bleibt beim Umsortieren erhalten. */
function sortLinkHtml(
    string $path, string $column, string $label,
    string $curSort, string $curDir, bool $foldersFirst
): string {
    $newDir = ($curSort === $column && $curDir === 'asc') ? 'desc' : 'asc';
    $url = '/web.php?path=' . urlencode($path)
         . '&sort=' . urlencode($column) . '&dir=' . urlencode($newDir)
         . '&foldersfirst=' . ($foldersFirst ? '1' : '0');
    $arrow = '';
    if ($curSort === $column) {
        $arrow = ' <span class="sort-arrow">' . ($curDir === 'asc' ? '▲' : '▼') . '</span>';
    }
    return '<a href="' . h($url) . '" class="sort-link">' . h($label) . $arrow . '</a>';
}

/* ── HTTPS enforce (AMICLOUD_ALLOW_HTTP: nur lokale Entwicklung) ─────── */
if ((empty($_SERVER['HTTPS']) || $_SERVER['HTTPS'] === 'off')
    && !(defined('AMICLOUD_ALLOW_HTTP') && AMICLOUD_ALLOW_HTTP)) {
    header('Location: https://' . $_SERVER['HTTP_HOST'] . $_SERVER['REQUEST_URI']);
    exit;
}

/* ── Logout ──────────────────────────────────────────────────────────── */
if (isset($_GET['logout'])) {
    session_destroy();
    header('Location: /web.php');
    exit;
}

/* ── Login ───────────────────────────────────────────────────────────── */
$loginError = '';

if (!isset($_SESSION['user_id'])) {
    if (empty($_SESSION['csrf'])) {
        $_SESSION['csrf'] = bin2hex(random_bytes(16));
    }

    if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['username'])) {
        if (!hash_equals($_SESSION['csrf'], $_POST['csrf'] ?? '')) {
            $loginError = 'Ungültige Anfrage.';
        } else {
            $stmt = DB::get()->prepare(
                'SELECT id, pass_hash FROM users WHERE username = ? AND active = 1 LIMIT 1'
            );
            $stmt->execute([trim($_POST['username'])]);
            $row = $stmt->fetch();

            if ($row && password_verify($_POST['password'] ?? '', $row['pass_hash'])) {
                session_regenerate_id(true);
                $_SESSION['user_id']  = (int)$row['id'];
                $_SESSION['username'] = trim($_POST['username']);
                $_SESSION['csrf']     = bin2hex(random_bytes(16));
                header('Location: /web.php');
                exit;
            }
            $loginError = 'Ungültige Zugangsdaten.';
        }
    }

    renderLogin($loginError);
    exit;
}

/* ── Auth state für FileStore setzen ─────────────────────────────────── */
Auth::$user_id  = $_SESSION['user_id'];
Auth::$username = $_SESSION['username'];

/* ── Aktion und Pfad bestimmen ───────────────────────────────────────── */
$action = $_GET['action'] ?? '';
$path   = sanitizePath($_GET['path'] ?? '/');

/* ── Download (GET, kein CSRF nötig — read-only) ────────────────────── */
if ($action === 'download') {
    FileStore::downloadRaw($path);
    exit;
}

/* ── CSRF-Prüfung für alle POST-Aktionen ─────────────────────────────── */
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    if (!hash_equals($_SESSION['csrf'] ?? '', $_POST['csrf'] ?? '')) {
        http_response_code(403);
        exit('Ungültige Anfrage (CSRF).');
    }
}

/* ── Upload (mehrere Dateien auf einmal, Abschnitt 22.1; Ordner-Upload) ──
 * Das Formular schickt immer name="file[]" (Array), auch bei nur einer
 * ausgewaehlten Datei. Ein Alt-Request mit skalarem name="file" wird der
 * Robustheit halber trotzdem normalisiert. Fehler einzelner Dateien
 * ueberspringen nur diese Datei, brechen aber nicht den ganzen Batch ab.
 *
 * Ordner-Upload (renderBrowser()'s <script>) schickt hier bewusst
 * "unterordner/datei.ext" als Dateiname (FormData.append() mit
 * File.webkitRelativePath statt File.name) -- PHP reicht $_FILES[...]
 * ['name'] unveraendert durch, inkl. Schraegstrichen, das ist keine
 * PHP-Eigenheit sondern genau das erwartete Verhalten. Anders als vorher
 * (basename()) werden Unterordner-Anteile daher NICHT mehr abgeschnitten,
 * nur noch nach denselben Regeln wie sanitizePath() gegen Path-Traversal/
 * Sonderzeichen geprueft -- der normale Mehrfachdatei-Upload schickt
 * ohnehin nie einen "/" im Namen (File.name enthaelt nie einen Pfad),
 * verhaelt sich also unveraendert wie zuvor. */
if ($_SERVER['REQUEST_METHOD'] === 'POST' && isset($_FILES['file'])) {
    $names    = $_FILES['file']['name'];
    $tmpNames = $_FILES['file']['tmp_name'];
    $errors   = $_FILES['file']['error'];
    if (!is_array($names)) {
        $names    = [$names];
        $tmpNames = [$tmpNames];
        $errors   = [$errors];
    }

    $uploaded = 0;
    $failed   = [];
    foreach ($names as $i => $rawName) {
        $err = $errors[$i] ?? UPLOAD_ERR_NO_FILE;
        if ($err === UPLOAD_ERR_NO_FILE) {
            continue;
        }
        $relname  = trim(str_replace('\\', '/', (string)$rawName), '/');
        $segments = array_filter(explode('/', $relname), function (string $p): bool {
            return $p !== '' && $p !== '.';
        });
        $badName = $relname === '' || in_array('..', $segments, true)
            || preg_match('/[\x00<>:"|?*]/', $relname);
        if ($err !== UPLOAD_ERR_OK || $badName) {
            $failed[] = $relname !== '' ? $relname : ('Datei ' . ($i + 1));
            continue;
        }
        $dest = sanitizePath($path . '/' . $relname);
        if (FileStore::uploadRaw($dest, $tmpNames[$i])) {
            $uploaded++;
        } else {
            $failed[] = $relname;
        }
    }

    $qs = 'path=' . urlencode($path) . '&uploaded=' . $uploaded;
    if ($failed !== []) {
        $qs .= '&failed=' . urlencode(implode(', ', $failed));
    }
    header('Location: /web.php?' . $qs);
    exit;
}

/* ── Löschen (immer rekursiv — fuer Dateien macht das Flag keinen
   Unterschied, fuer Ordner loescht es jetzt tatsaechlich den Inhalt statt
   still an einem rmdir() auf ein nicht-leeres Verzeichnis zu scheitern) ── */
if ($action === 'delete' && $_SERVER['REQUEST_METHOD'] === 'POST') {
    $ok     = FileStore::deleteRaw($path, true);
    $parent = dirname($path);
    if ($parent === '.' || $parent === '') {
        $parent = '/';
    }
    $qs = 'path=' . urlencode($parent);
    if (!$ok) {
        $qs .= '&error=' . urlencode(basename($path) . ' konnte nicht gelöscht werden.');
    }
    header('Location: /web.php?' . $qs);
    exit;
}

/* ── Mehrfach-Löschen (Checkbox-Auswahl) ───────────────────────────────── */
if ($action === 'delete_bulk' && $_SERVER['REQUEST_METHOD'] === 'POST') {
    $paths = $_POST['paths'] ?? [];
    if (!is_array($paths)) {
        $paths = [$paths];
    }

    $done = 0;
    $fail = 0;
    foreach ($paths as $rawPath) {
        $p = sanitizePath((string)$rawPath);
        if (FileStore::deleteRaw($p, true)) {
            $done++;
        } else {
            $fail++;
        }
    }

    $qs = 'path=' . urlencode($path);
    if ($done > 0 || $fail > 0) {
        $qs .= '&bulkmsg=' . urlencode(
            $done . ($done === 1 ? ' Element gelöscht' : ' Elemente gelöscht')
            . ($fail > 0 ? ", $fail Fehler" : '')
        );
    }
    header('Location: /web.php?' . $qs);
    exit;
}

/* ── Ordner anlegen ──────────────────────────────────────────────────── */
if ($action === 'mkdir' && $_SERVER['REQUEST_METHOD'] === 'POST') {
    $dirname = trim($_POST['dirname'] ?? '');
    if ($dirname !== '') {
        FileStore::mkdirRaw(sanitizePath($path . '/' . $dirname));
    }
    header('Location: /web.php?path=' . urlencode($path));
    exit;
}

/* ── Umbenennen ──────────────────────────────────────────────────────── */
if ($action === 'rename' && $_SERVER['REQUEST_METHOD'] === 'POST') {
    $newname = basename(trim($_POST['newname'] ?? ''));
    $parent  = dirname($path);
    if ($parent === '.' || $parent === '') {
        $parent = '/';
    }
    if ($newname !== '' && $newname !== '.' && $newname !== '..') {
        FileStore::renameRaw($path, sanitizePath($parent . '/' . $newname));
    }
    header('Location: /web.php?path=' . urlencode($parent));
    exit;
}

/* ── Verzeichnislisting ──────────────────────────────────────────────── */
$entries = FileStore::listRaw($path);

/* Sortierung ist reine Web-UI-Praesentation (nicht Teil des JSON-API-
 * Listings, das amicloud/-shell/-commander nutzen -- deren erwartete
 * Namens-Sortierreihenfolge aus FileStore::scanEntries() bleibt
 * unveraendert) -- deshalb hier auf der bereits geladenen Liste, nicht in
 * FileStore. */
$sort         = in_array($_GET['sort'] ?? '', ['name', 'size', 'modified'], true)
              ? $_GET['sort'] : 'name';
$sortDir      = ($_GET['dir'] ?? '') === 'desc' ? 'desc' : 'asc';
$foldersFirst = ($_GET['foldersfirst'] ?? '1') !== '0';

usort($entries, function (array $a, array $b) use ($sort, $sortDir, $foldersFirst): int {
    if ($foldersFirst) {
        $aDir = $a['type'] === 'dir';
        $bDir = $b['type'] === 'dir';
        if ($aDir !== $bDir) {
            return $aDir ? -1 : 1;
        }
    }
    switch ($sort) {
        case 'size':     $cmp = $a['size'] <=> $b['size']; break;
        case 'modified': $cmp = $a['mtime'] <=> $b['mtime']; break;
        default:          $cmp = strcasecmp($a['name'], $b['name']);
    }
    return $sortDir === 'desc' ? -$cmp : $cmp;
});

$uploadedCount = isset($_GET['uploaded']) ? (int)$_GET['uploaded'] : null;
$failedNames   = (string)($_GET['failed'] ?? '');
$deleteError   = (string)($_GET['error'] ?? '');
$bulkMsg       = (string)($_GET['bulkmsg'] ?? '');
renderBrowser($_SESSION['username'], $path, $entries, $_SESSION['csrf'],
             $sort, $sortDir, $foldersFirst,
             $uploadedCount, $failedNames, $deleteError, $bulkMsg);


/* ════════════════════════════════════════════════════════════════════════
   Render: Login
   ════════════════════════════════════════════════════════════════════════ */
function renderLogin(string $error): void
{
    $csrf = $_SESSION['csrf'];
?><!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AmiCloud v<?= AMICLOUD_VERSION ?> — Login</title>
  <link rel="stylesheet" href="/web.css">
</head>
<body class="login-page">
  <div class="login-box">
    <h1><span class="c-ami">Ami</span><span class="c-cloud">Cloud</span></h1>
    <?php if ($error !== ''): ?>
      <div class="msg-error"><?= h($error) ?></div>
    <?php endif; ?>
    <form method="post" action="/web.php">
      <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
      <label>Benutzer
        <input type="text" name="username" autofocus autocomplete="username">
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


/* ════════════════════════════════════════════════════════════════════════
   Render: Dateibrowser
   ════════════════════════════════════════════════════════════════════════ */
function renderBrowser(
    string $username,
    string $path,
    array $entries,
    string $csrf,
    string $sort,
    string $sortDir,
    bool $foldersFirst,
    ?int $uploadedCount = null,
    string $failedNames = '',
    string $deleteError = '',
    string $bulkMsg = ''
): void {
    $parentPath = dirname($path);
    if ($parentPath === '.' || $parentPath === '') {
        $parentPath = '/';
    }

    $uploadHint = null;
    if ($uploadedCount !== null) {
        $uploadHint = $uploadedCount === 1 ? '1 Datei hochgeladen.' : "$uploadedCount Dateien hochgeladen.";
        if ($failedNames !== '') {
            $uploadHint .= ' Fehlgeschlagen: ' . $failedNames;
        }
    } elseif ($failedNames !== '') {
        $uploadHint = 'Hochladen fehlgeschlagen: ' . $failedNames;
    }
?><!DOCTYPE html>
<html lang="de">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AmiCloud v<?= AMICLOUD_VERSION ?> — <?= h($path) ?></title>
  <link rel="stylesheet" href="/web.css">
</head>
<body>

  <header class="topbar">
    <a href="/web.php" class="brand"><span class="c-ami">Ami</span><span class="c-cloud">Cloud</span></a>
    <span class="userinfo">
      <?= h($username) ?>
      <a href="/web.php?logout" class="btn-logout">Abmelden</a>
    </span>
  </header>

  <main>
    <nav class="breadcrumb"><?= breadcrumbHtml($path) ?></nav>

    <?php if ($uploadHint !== null): ?>
    <div class="msg-info"><?= h($uploadHint) ?></div>
    <?php endif; ?>
    <?php if ($deleteError !== ''): ?>
    <div class="msg-error"><?= h($deleteError) ?></div>
    <?php endif; ?>
    <?php if ($bulkMsg !== ''): ?>
    <div class="msg-info"><?= h($bulkMsg) ?></div>
    <?php endif; ?>

    <label class="foldersfirst-toggle">
      <input type="checkbox" <?= $foldersFirst ? 'checked' : '' ?>
             onchange="location.href='/web.php?path=<?= urlencode($path) ?>&amp;sort=<?= urlencode($sort) ?>&amp;dir=<?= urlencode($sortDir) ?>&amp;foldersfirst=' + (this.checked ? '1' : '0')">
      Ordner zuerst
    </label>

    <table class="filetable">
      <thead>
        <tr>
          <th class="col-check"><input type="checkbox" id="checkAll" title="Alle sichtbaren an-/abwählen"></th>
          <th><?= sortLinkHtml($path, 'name', 'Name', $sort, $sortDir, $foldersFirst) ?></th>
          <th><?= sortLinkHtml($path, 'size', 'Größe', $sort, $sortDir, $foldersFirst) ?></th>
          <th class="col-md5">MD5</th>
          <th><?= sortLinkHtml($path, 'modified', 'Geändert', $sort, $sortDir, $foldersFirst) ?></th>
          <th></th>
        </tr>
      </thead>
      <tbody>

        <?php if ($path !== '/'): ?>
        <tr class="row-dir">
          <td></td>
          <td colspan="4">
            <a href="/web.php?path=<?= urlencode($parentPath) ?>" class="link-dir">.. (übergeordnet)</a>
          </td>
          <td></td>
        </tr>
        <?php endif; ?>

        <?php foreach ($entries as $e):
            $epath = rtrim($path, '/') . '/' . $e['name'];
            /* Vorschau-Zahlen fuer die Rueckfrage(n) — bei Dateien trivial
             * (kein Query noetig), bei Ordnern per countAndSize() (eine
             * Query pro sichtbarem Ordner in der aktuellen Ansicht). */
            if ($e['type'] === 'dir') {
                $counts = FileStore::countAndSize($epath);
            } else {
                $counts = ['files' => 1, 'dirs' => 0, 'bytes' => $e['size']];
            }
            $confirmText = $e['type'] === 'dir'
                ? $e['name'] . ' löschen? (' . $counts['dirs'] . ' Ordner, ' . $counts['files']
                    . ' Dateien, ' . fmtSize($counts['bytes']) . ')'
                : $e['name'] . ' löschen? (' . fmtSize($counts['bytes']) . ')';
        ?>
        <tr class="row-<?= $e['type'] ?>">
          <td class="col-check">
            <input type="checkbox" name="paths[]" value="<?= h($epath) ?>" form="bulk-form"
                   data-files="<?= $counts['files'] ?>" data-dirs="<?= $counts['dirs'] ?>"
                   data-bytes="<?= $counts['bytes'] ?>" data-name="<?= h($e['name']) ?>">
          </td>
          <td<?= ($e['comment'] ?? '') !== '' ? ' title="' . h($e['comment']) . '"' : '' ?>>
            <?php if ($e['type'] === 'dir'): ?>
              <a href="/web.php?path=<?= urlencode($epath) ?>" class="link-dir"><?= h($e['name']) ?></a>
            <?php else: ?>
              <a href="/web.php?action=download&amp;path=<?= urlencode($epath) ?>" class="link-file"><?= h($e['name']) ?></a>
            <?php endif; ?>
          </td>
          <td class="col-meta"><?= $e['type'] === 'dir' ? '—' : h(fmtSize($e['size'])) ?></td>
          <td class="col-meta col-md5">
            <?php if (!empty($e['md5'])): ?>
              <span class="md5-cell" title="<?= h($e['md5']) ?>"><?= h($e['md5']) ?></span>
            <?php else: ?>
              —
            <?php endif; ?>
          </td>
          <td class="col-meta"><?= h(substr($e['modified'], 0, 16)) ?></td>
          <td class="col-act">
            <form method="post" class="form-inline"
                  action="/web.php?action=rename&amp;path=<?= urlencode($epath) ?>"
                  onsubmit="var n=prompt(<?= json_encode($e['name'] . ' umbenennen in:') ?>, <?= json_encode($e['name']) ?>); if(!n) return false; this.newname.value=n; return true;">
              <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
              <input type="hidden" name="newname" value="">
              <button type="submit" class="btn-rename">Umbenennen</button>
            </form>
            <form method="post" class="form-inline"
                  action="/web.php?action=delete&amp;path=<?= urlencode($epath) ?>"
                  onsubmit="return confirm(<?= json_encode($confirmText) ?>)">
              <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
              <button type="submit" class="btn-del">Löschen</button>
            </form>
          </td>
        </tr>
        <?php endforeach; ?>

        <?php if (empty($entries)): ?>
        <tr><td colspan="6" class="empty">Ordner ist leer</td></tr>
        <?php endif; ?>

      </tbody>
    </table>

    <form id="bulk-form" method="post" action="/web.php?action=delete_bulk&amp;path=<?= urlencode($path) ?>"
          onsubmit="return confirmBulkDelete(this)">
      <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
    </form>

    <div class="actions">
      <button type="submit" form="bulk-form" class="btn-del">Ausgewählte löschen</button>

      <form method="post" id="upload-form"
            action="/web.php?path=<?= urlencode($path) ?>"
            enctype="multipart/form-data"
            class="form-row">
        <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
        <input type="file" id="fileInput" name="file[]" multiple hidden>
        <input type="file" id="folderInput" webkitdirectory hidden>
        <button type="button" id="filePickBtn" class="btn-pick">Datei hochladen</button>
        <button type="button" id="folderPickBtn" class="btn-pick">Ordner hochladen</button>
      </form>

      <form method="post"
            action="/web.php?action=mkdir&amp;path=<?= urlencode($path) ?>"
            class="form-row">
        <input type="hidden" name="csrf" value="<?= h($csrf) ?>">
        <input type="text" name="dirname" placeholder="Neuer Ordner" required>
        <button type="submit">Erstellen</button>
      </form>
    </div>
  </main>

  <footer class="pagefooter">
    AmiCloud v<?= AMICLOUD_VERSION ?>
  </footer>

  <script>
    /* Summiert die data-files/data-dirs/data-bytes der angehakten Zeilen
     * (siehe die Checkbox-Spalte oben) und fragt mit der Summe nach —
     * kann nur im Browser passieren, die Auswahl ist serverseitig zum
     * Renderzeitpunkt noch nicht bekannt. */
    function confirmBulkDelete(form) {
      var boxes = document.querySelectorAll('input[name="paths[]"]:checked');
      if (boxes.length === 0) {
        alert('Bitte mindestens ein Element auswählen.');
        return false;
      }
      var files = 0, dirs = 0, bytes = 0;
      boxes.forEach(function (cb) {
        files += parseInt(cb.dataset.files, 10) || 0;
        dirs  += parseInt(cb.dataset.dirs, 10) || 0;
        bytes += parseInt(cb.dataset.bytes, 10) || 0;
      });
      var size = bytes < 1024 ? bytes + ' B'
               : bytes < 1048576 ? Math.round(bytes / 1024 * 10) / 10 + ' KB'
               : bytes < 1073741824 ? Math.round(bytes / 1048576 * 10) / 10 + ' MB'
               : Math.round(bytes / 1073741824 * 10) / 10 + ' GB';
      return confirm(
        boxes.length + ' ausgewählte(s) Element(e) löschen? ('
        + dirs + ' Ordner, ' + files + ' Dateien, ' + size + ')'
      );
    }

    /* Kopfzeilen-Checkbox: an/abwählen aller GERADE SICHTBAREN Zeilen
     * (also aller "paths[]"-Checkboxen im aktuell geladenen Listing). */
    document.getElementById('checkAll').addEventListener('change', function () {
      var checked = this.checked;
      document.querySelectorAll('input[name="paths[]"]').forEach(function (cb) {
        cb.checked = checked;
      });
    });

    /* Upload-Widget: ein Formular, zwei Auswahlwege (Dateien/Ordner) --
     * beide ueber ausgeblendete native <input type="file">, ausgeloest
     * durch ein Label bzw. einen Button. Einstufig: die Dateiauswahl
     * selbst laedt (nach kurzer confirm()-Rueckfrage) direkt hoch, kein
     * separater "Hochladen"-Klick.
     *
     * Ordner-Upload braucht zwingend JS/fetch(): eine normale <form>-
     * Einreichung sendet fuer jede Datei nur ihren blossen Namen (File.name),
     * die von webkitdirectory gesetzte Unterordner-Info (File.
     * webkitRelativePath) wird dabei NIE mitgeschickt -- nur FormData.
     * append(feld, datei, dateiname) erlaubt, den gesendeten Dateinamen frei
     * zu setzen (hier: den vollen relativen Pfad "unterordner/datei.ext"),
     * den web.php's Upload-Handler dann als solchen erkennt und die
     * Unterordner serverseitig anlegt (siehe FileStore::uploadRaw()). Der
     * normale Datei-Upload nutzt dagegen ein ganz normales natives POST
     * (form.submit()) -- kein fetch() noetig, File.name hat ohnehin nie
     * einen Pfad drin. */
    (function () {
      var form       = document.getElementById('upload-form');
      var fileBtn     = document.getElementById('filePickBtn');
      var fileInput   = document.getElementById('fileInput');
      var folderBtn   = document.getElementById('folderPickBtn');
      var folderInput = document.getElementById('folderInput');

      function countText(n) {
        return n + (n === 1 ? ' Datei' : ' Dateien');
      }

      fileBtn.addEventListener('click', function () { fileInput.click(); });

      fileInput.addEventListener('change', function () {
        if (fileInput.files.length === 0) return;
        if (!confirm(countText(fileInput.files.length) + ' hochladen?')) {
          fileInput.value = '';
          return;
        }
        form.submit();
      });

      folderBtn.addEventListener('click', function () { folderInput.click(); });

      folderInput.addEventListener('change', function () {
        if (folderInput.files.length === 0) return;
        if (!confirm(countText(folderInput.files.length) + ' aus dem Ordner hochladen?')) {
          folderInput.value = '';
          return;
        }

        var data = new FormData();
        data.append('csrf', form.elements.csrf.value);
        var files = folderInput.files;
        for (var i = 0; i < files.length; i++) {
          data.append('file[]', files[i], files[i].webkitRelativePath || files[i].name);
        }

        folderBtn.disabled = true;
        folderBtn.textContent = 'Lädt hoch…';
        fetch(form.action, { method: 'POST', body: data })
          .then(function (resp) { window.location.href = resp.url; })
          .catch(function () {
            folderBtn.disabled = false;
            folderBtn.textContent = 'Ordner hochladen';
            alert('Hochladen fehlgeschlagen.');
          });
      });
    })();
  </script>

</body>
</html>
<?php
}
