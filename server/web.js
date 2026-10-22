/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 *
 * Weboberflaeche: Menues, Dialoge, Auswahl, Details/Teilen, Upload mit
 * Fortschritt und Drag & Drop. Ohne Framework; alle Aenderungen laufen
 * ueber POST an web.php (CSRF-Token aus der Seite). */
(function () {
  'use strict';

  var $  = function (sel, root) { return (root || document).querySelector(sel); };
  var $$ = function (sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); };

  function csrf() {
    var el = $('input[name="csrf"]');
    return el ? el.value : '';
  }

  function post(fields) {
    var data = new FormData();
    data.append('csrf', csrf());
    Object.keys(fields).forEach(function (k) { data.append(k, fields[k]); });
    return fetch('/web.php', { method: 'POST', body: data, headers: { 'X-Requested-With': 'fetch' }, credentials: 'same-origin' })
      .then(function (r) { return r.json(); });
  }

  function getJson(params) {
    return fetch('/web.php?' + new URLSearchParams(params), { headers: { 'X-Requested-With': 'fetch' }, credentials: 'same-origin' })
      .then(function (r) { return r.json(); });
  }

  function fmtSize(b) {
    if (b < 1024) return b + ' B';
    if (b < 1048576) return (Math.round(b / 102.4) / 10 + ' KB').replace('.', ',');
    if (b < 1073741824) return (Math.round(b / 104857.6) / 10 + ' MB').replace('.', ',');
    return (Math.round(b / 107374182.4) / 10 + ' GB').replace('.', ',');
  }

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }

  /* ── Menues ──────────────────────────────────────────────────────── */
  function closeMenus(except) {
    $$('.menu').forEach(function (m) { if (m !== except) m.hidden = true; });
    $$('[data-menu]').forEach(function (b) { b.setAttribute('aria-expanded', 'false'); });
  }

  document.addEventListener('click', function (ev) {
    var btn = ev.target.closest('[data-menu]');
    if (btn) {
      var menu = document.getElementById(btn.dataset.menu);
      var open = menu.hidden;
      closeMenus(menu);
      menu.hidden = !open;
      btn.setAttribute('aria-expanded', open ? 'true' : 'false');
      ev.stopPropagation();
      return;
    }
    if (!ev.target.closest('.menu') && !ev.target.closest('[data-row-menu]')) closeMenus();
  });
  document.addEventListener('keydown', function (ev) {
    if (ev.key === 'Escape') { closeMenus(); closeDetails(); }
  });

  /* Navigation auf schmalen Bildschirmen */
  $$('[data-toggle-nav]').forEach(function (b) {
    b.addEventListener('click', function () { document.body.classList.toggle('nav-open'); });
  });

  $$('[data-dismiss]').forEach(function (b) {
    b.addEventListener('click', function () { b.parentNode.remove(); });
  });

  $$('form[data-confirm]').forEach(function (f) {
    f.addEventListener('submit', function (ev) { if (!confirm(f.dataset.confirm)) ev.preventDefault(); });
  });

  /* Kopieren: naechstes [data-copy-src] im selben Elternelement */
  document.addEventListener('click', function (ev) {
    var b = ev.target.closest('[data-copy]');
    if (!b) return;
    var src = b.parentNode.querySelector('[data-copy-src]');
    if (!src) return;
    var text = src.textContent.trim();
    var done = function () { var t = b.title; b.classList.add('copied'); setTimeout(function () { b.classList.remove('copied'); }, 1200); };
    if (navigator.clipboard) {
      navigator.clipboard.writeText(text).then(done, function () { selectText(src); });
    } else {
      selectText(src);
    }
  });
  function selectText(node) {
    var r = document.createRange(); r.selectNodeContents(node);
    var s = window.getSelection(); s.removeAllRanges(); s.addRange(r);
  }

  /* ── Dialoge allgemein ───────────────────────────────────────────── */
  $$('[data-open-dialog]').forEach(function (b) {
    b.addEventListener('click', function () { document.getElementById(b.dataset.openDialog).showModal(); });
  });
  document.addEventListener('click', function (ev) {
    var b = ev.target.closest('[data-close-dialog]');
    if (b) b.closest('dialog').close();
  });

  /* Kontingent-Auswahl: "Eigener Wert" blendet das Textfeld ein */
  function wireQuota(root) {
    $$('[data-quota-select]', root).forEach(function (sel) {
      var custom = sel.parentNode.querySelector('[data-quota-custom]');
      sel.addEventListener('change', function () {
        custom.hidden = sel.value !== 'custom';
        if (!custom.hidden) custom.focus();
      });
    });
  }
  wireQuota(document);

  /* ── Benutzerverwaltung ──────────────────────────────────────────── */
  var dlgEdit = document.getElementById('dlgUserEdit');
  if (dlgEdit) {
    $$('[data-edit-user]').forEach(function (b) {
      b.addEventListener('click', function () {
        var u = JSON.parse(b.dataset.editUser);
        var f = $('form', dlgEdit);
        f.id.value = u.id;
        f.display_name.value = u.display_name;
        f.email.value = u.email;
        f.password.value = '';
        f.is_admin.checked = u.is_admin;
        f.active.checked = u.active;
        $('[data-username]', dlgEdit).textContent = u.username;
        var slot = $('[data-quota-slot]', dlgEdit);
        slot.innerHTML = '';
        slot.appendChild(document.getElementById('tplQuota').content.cloneNode(true));
        var sel = $('select', slot), custom = $('[data-quota-custom]', slot);
        var opt = u.quota === null ? '' : String(u.quota);
        if ($$('option', sel).some(function (o) { return o.value === opt; })) {
          sel.value = opt;
        } else {
          sel.value = 'custom'; custom.hidden = false; custom.value = fmtSize(u.quota);
        }
        wireQuota(slot);
        dlgEdit.showModal();
      });
    });
    var dlgDel = document.getElementById('dlgUserDelete');
    $$('[data-delete-user]').forEach(function (b) {
      b.addEventListener('click', function () {
        var u = JSON.parse(b.dataset.deleteUser);
        var f = $('form', dlgDel);
        f.id.value = u.id;
        f.confirm.value = '';
        $('[data-username]', dlgDel).textContent = u.username;
        dlgDel.showModal();
      });
    });
  }

  /* ── Dateiansicht ────────────────────────────────────────────────── */
  var files = $('.files');
  if (!files) return;

  var curDir   = files.dataset.dir;
  var actForm  = document.getElementById('actForm');
  var rowMenu  = document.getElementById('rowmenu');
  var menuRow  = null;

  /* POST ueber das versteckte Formular (Seite laedt danach neu) */
  function submitAct(doName, fields, paths) {
    $$('input.dyn', actForm).forEach(function (i) { i.remove(); });
    actForm.elements['do'].value = doName;
    var add = function (name, value) {
      var i = document.createElement('input');
      i.type = 'hidden'; i.name = name; i.value = value; i.className = 'dyn';
      actForm.appendChild(i);
    };
    Object.keys(fields || {}).forEach(function (k) { add(k, fields[k]); });
    (paths || []).forEach(function (p) { add('paths[]', p); });
    actForm.submit();
  }

  function rowData(tr) {
    return {
      path: tr.dataset.path, name: tr.dataset.name, type: tr.dataset.type,
      files: +tr.dataset.files, dirs: +tr.dataset.dirs, bytes: +tr.dataset.bytes
    };
  }

  /* Namensdialog (Neuer Ordner, Umbenennen) */
  function askName(title, label, okText, value, cb) {
    var d = document.getElementById('dlgName');
    $('[data-title]', d).textContent = title;
    $('[data-label]', d).textContent = label;
    $('[data-ok]', d).textContent = okText;
    var input = $('input[name="name"]', d);
    input.value = value || '';
    d.returnValue = '';
    d.onclose = function () {
      var v = input.value.trim();
      if (d.returnValue === 'ok' && v !== '') cb(v);
    };
    d.showModal();
    input.focus();
    var dot = value ? value.lastIndexOf('.') : -1;
    input.setSelectionRange(0, dot > 0 ? dot : (value || '').length);
  }

  function askConfirm(title, text, okText, cb) {
    var d = document.getElementById('dlgConfirm');
    $('[data-title]', d).textContent = title;
    $('[data-text]', d).textContent = text;
    $('[data-ok]', d).textContent = okText;
    d.returnValue = '';
    d.onclose = function () { if (d.returnValue === 'ok') cb(); };
    d.showModal();
  }

  function confirmDelete(rows) {
    var files = 0, dirs = 0, bytes = 0;
    rows.forEach(function (r) { files += r.files; dirs += r.dirs; bytes += r.bytes; });
    var what = rows.length === 1 ? '„' + rows[0].name + '“' : rows.length + ' Elemente';
    var detail = rows.length === 1 && rows[0].type === 'file'
      ? fmtSize(bytes)
      : dirs + ' Ordner, ' + files + ' Dateien, ' + fmtSize(bytes);
    askConfirm(what + ' löschen?', 'Das lässt sich nicht rückgängig machen (' + detail + ').', 'Löschen', function () {
      submitAct('delete', {}, rows.map(function (r) { return r.path; }));
    });
  }

  /* ── Auswahl ─────────────────────────────────────────────────────── */
  var selBar  = $('[data-selectionbar]');
  var toolbar = $('[data-toolbar]');
  var checkAll = $('[data-check-all]');

  function selectedRows() {
    return $$('input.sel:checked').map(function (c) { return rowData(c.closest('tr')); });
  }
  function updateSelection() {
    var rows = selectedRows();
    selBar.hidden = rows.length === 0;
    toolbar.hidden = rows.length > 0;
    $('[data-selcount]', selBar).textContent = rows.length + ' ausgewählt';
    $$('tbody tr').forEach(function (tr) {
      var c = $('input.sel', tr);
      tr.classList.toggle('selected', !!(c && c.checked));
    });
    var all = $$('input.sel');
    checkAll.checked = all.length > 0 && rows.length === all.length;
    checkAll.indeterminate = rows.length > 0 && rows.length < all.length;
  }
  files.addEventListener('change', function (ev) {
    if (ev.target.matches('input.sel')) updateSelection();
  });
  checkAll.addEventListener('change', function () {
    $$('input.sel').forEach(function (c) { c.checked = checkAll.checked; });
    updateSelection();
  });
  /* Klick auf die Zeile (nicht auf Link/Knopf) waehlt aus */
  files.addEventListener('click', function (ev) {
    var tr = ev.target.closest('tbody tr[data-path]');
    if (!tr || ev.target.closest('a, button, input')) return;
    var c = $('input.sel', tr);
    c.checked = !c.checked;
    updateSelection();
  });

  var foldersFirst = $('[data-foldersfirst]');
  if (foldersFirst) {
    foldersFirst.addEventListener('change', function () {
      location.href = foldersFirst.dataset.url + '&foldersfirst=' + (foldersFirst.checked ? '1' : '0');
    });
  }

  /* ── Zeilenmenue ─────────────────────────────────────────────────── */
  files.addEventListener('click', function (ev) {
    var b = ev.target.closest('[data-row-menu]');
    if (!b) return;
    ev.stopPropagation();
    var tr = b.closest('tr');
    if (!rowMenu.hidden && menuRow === tr) { rowMenu.hidden = true; return; }
    closeMenus();
    menuRow = tr;
    $('[data-row-action="download"]', rowMenu).hidden = tr.dataset.type !== 'file';
    rowMenu.hidden = false;
    var r = b.getBoundingClientRect();
    var top = r.bottom + window.scrollY + 4;
    var left = Math.min(r.right + window.scrollX - rowMenu.offsetWidth, document.documentElement.clientWidth - rowMenu.offsetWidth - 8);
    if (r.bottom + rowMenu.offsetHeight + 8 > window.innerHeight) top = r.top + window.scrollY - rowMenu.offsetHeight - 4;
    rowMenu.style.top = top + 'px';
    rowMenu.style.left = Math.max(8, left) + 'px';
  });

  rowMenu.addEventListener('click', function (ev) {
    var b = ev.target.closest('[data-row-action]');
    if (!b || !menuRow) return;
    rowMenu.hidden = true;
    var row = rowData(menuRow);
    switch (b.dataset.rowAction) {
      case 'details':  openDetails(row, 'info'); break;
      case 'share':    openDetails(row, 'share'); break;
      case 'download': location.href = $('a.name', menuRow).href; break;
      case 'rename':
        askName('Umbenennen', 'Neuer Name', 'Umbenennen', row.name, function (v) {
          submitAct('rename', { path: row.path, name: v });
        });
        break;
      case 'move':   openMove([row]); break;
      case 'delete': confirmDelete([row]); break;
    }
  });

  /* ── Werkzeugleiste / Auswahlleiste ──────────────────────────────── */
  document.addEventListener('click', function (ev) {
    var b = ev.target.closest('[data-action]');
    if (!b) return;
    closeMenus();
    var rows = selectedRows();
    switch (b.dataset.action) {
      case 'mkdir':
        askName('Neuer Ordner', 'Name', 'Anlegen', '', function (v) { submitAct('mkdir', { dir: curDir, name: v }); });
        break;
      case 'pick-files':  document.getElementById('fileInput').click(); break;
      case 'pick-folder': document.getElementById('folderInput').click(); break;
      case 'zip':    submitAct('zip', {}, rows.map(function (r) { return r.path; })); break;
      case 'move':   openMove(rows); break;
      case 'delete': confirmDelete(rows); break;
      case 'clear-selection':
        $$('input.sel').forEach(function (c) { c.checked = false; });
        updateSelection();
        break;
    }
  });

  /* ── Verschieben: Ordnerbaum, lazy aus dem Dateisystem ───────────── */
  function openMove(rows) {
    var d = document.getElementById('dlgMove');
    var tree = $('[data-tree]', d);
    var targetEl = $('[data-target]', d);
    var target = '/';
    var moving = rows.map(function (r) { return r.path; });
    $('[data-title]', d).textContent = (rows.length === 1 ? '„' + rows[0].name + '“' : rows.length + ' Elemente') + ' verschieben nach …';
    tree.innerHTML = '';

    function node(path, label, parentUl) {
      var li = el('li');
      var btn = el('button', 'tree-node');
      btn.type = 'button';
      btn.innerHTML = '<svg class="ic" aria-hidden="true"><use href="#i-folder"></use></svg>';
      btn.appendChild(document.createTextNode(label));
      var blocked = moving.some(function (m) { return path === m || path.indexOf(m + '/') === 0; });
      if (blocked) btn.disabled = true;
      var sub = el('ul');
      var loaded = false;
      btn.addEventListener('click', function () {
        $$('.tree-node.sel', tree).forEach(function (n) { n.classList.remove('sel'); });
        btn.classList.add('sel');
        target = path;
        targetEl.textContent = path;
        if (loaded) { sub.hidden = !sub.hidden; return; }
        loaded = true;
        getJson({ ajax: 'dirs', path: path }).then(function (res) {
          (res.dirs || []).forEach(function (name) {
            node((path === '/' ? '' : path) + '/' + name, name, sub);
          });
        });
      });
      li.appendChild(btn);
      li.appendChild(sub);
      parentUl.appendChild(li);
      return btn;
    }
    var rootUl = el('ul');
    tree.appendChild(rootUl);
    node('/', 'Alle Dateien', rootUl).click();

    d.returnValue = '';
    d.onclose = function () {
      if (d.returnValue === 'ok') submitAct('move', { target: target }, moving);
    };
    d.showModal();
  }

  /* ── Details-Seitenleiste ────────────────────────────────────────── */
  var details = document.getElementById('details');
  var detailsRow = null;

  function closeDetails() {
    if (details) details.hidden = true;
    document.body.classList.remove('details-open');
  }
  if (details) $('[data-close-details]', details).addEventListener('click', closeDetails);

  function showTab(tab) {
    $$('[data-tab]', details).forEach(function (t) { t.setAttribute('aria-selected', t.dataset.tab === tab ? 'true' : 'false'); });
    $$('[data-panel]', details).forEach(function (p) { p.hidden = p.dataset.panel !== tab; });
  }
  if (details) {
    $$('[data-tab]', details).forEach(function (t) {
      t.addEventListener('click', function () { showTab(t.dataset.tab); });
    });
  }

  function openDetails(row, tab) {
    detailsRow = row;
    var kindSrc = $('tr[data-path="' + CSS.escape(row.path) + '"] .kind');
    $('[data-d-icon]', details).innerHTML = kindSrc ? kindSrc.innerHTML : '';
    $('[data-d-icon]', details).className = kindSrc ? kindSrc.className : 'kind';
    $('[data-d-name]', details).textContent = row.name;
    $('[data-d-sub]', details).textContent = row.path;
    showTab(tab);
    details.hidden = false;
    document.body.classList.add('details-open');
    loadDetails();
  }

  function loadDetails() {
    var row = detailsRow;
    getJson({ ajax: 'info', path: row.path }).then(function (res) {
      if (res.status !== 'ok') return;
      var meta = $('[data-d-meta]', details);
      meta.innerHTML = '';
      [['Größe', res.size], ['Geändert', res.modified], ['MD5', res.md5 || '–'], ['Protection', res.protection]]
        .forEach(function (kv) {
          meta.appendChild(el('dt', '', kv[0]));
          var dd = el('dd', kv[0] === 'MD5' || kv[0] === 'Protection' ? 'mono' : '', kv[1]);
          meta.appendChild(dd);
        });
      document.getElementById('dComment').value = res.comment || '';
      renderShares(res.shares || []);
    });
  }

  function renderShares(list) {
    var ul = $('[data-d-shares]', details);
    ul.innerHTML = '';
    if (list.length === 0) {
      ul.appendChild(el('li', 'muted', 'Noch kein öffentlicher Link.'));
      return;
    }
    list.forEach(function (s) {
      var li = el('li', 'share-item');
      var code = el('code', '', s.url);
      code.setAttribute('data-copy-src', '');
      li.appendChild(code);
      var copy = el('button', 'icon-btn');
      copy.type = 'button';
      copy.setAttribute('data-copy', '');
      copy.setAttribute('aria-label', 'Link kopieren');
      copy.innerHTML = '<svg class="ic"><use href="#i-copy"></use></svg>';
      li.appendChild(copy);
      var info = [];
      if (s.has_pass) info.push('mit Passwort');
      info.push(s.expires ? 'bis ' + s.expires : 'ohne Ablauf');
      info.push(s.downloads + ' Downloads');
      li.appendChild(el('small', '', info.join(' · ')));
      var del = el('button', 'btn btn-danger-ghost', 'Löschen');
      del.type = 'button';
      del.addEventListener('click', function () {
        post({ do: 'share_delete', id: s.id }).then(loadDetails);
      });
      li.appendChild(del);
      ul.appendChild(li);
    });
  }

  if (details) {
    $('[data-comment-form]', details).addEventListener('submit', function (ev) {
      ev.preventDefault();
      var v = document.getElementById('dComment').value;
      post({ do: 'setcomment', path: detailsRow.path, comment: v }).then(function () {
        var tr = $('tr[data-path="' + CSS.escape(detailsRow.path) + '"]');
        if (tr) {
          var c = $('.name-comment', tr);
          if (!c && v) { c = el('span', 'name-comment'); c.title = 'Amiga-Kommentar'; $('.col-name', tr).appendChild(c); }
          if (c) { c.textContent = v; c.hidden = !v; }
        }
        loadDetails();
      });
    });
    $('[data-share-form]', details).addEventListener('submit', function (ev) {
      ev.preventDefault();
      var pass = document.getElementById('sPass'), exp = document.getElementById('sExp');
      post({ do: 'share_create', path: detailsRow.path, pass: pass.value, expires: exp.value }).then(function (res) {
        if (res.status !== 'ok') { alert(res.message); return; }
        pass.value = ''; exp.value = '';
        if (navigator.clipboard) navigator.clipboard.writeText(res.url).catch(function () {});
        loadDetails();
      });
    });
  }

  /* ── Upload: Auswahl, Drag & Drop, Konflikte, Fortschritt ────────── */
  var existing = JSON.parse(files.dataset.existing || '[]');
  var uploadsBox = $('[data-uploads]');

  /* items: [{file, rel}] mit rel = Pfad relativ zum aktuellen Ordner */
  function startUpload(items) {
    if (items.length === 0) return;
    var clashes = [];
    items.forEach(function (it) {
      var top = it.rel.split('/')[0];
      if (existing.indexOf(top) !== -1 && clashes.indexOf(top) === -1) clashes.push(top);
    });
    if (clashes.length === 0) { runUpload(items); return; }

    var d = document.getElementById('dlgConflict');
    $('[data-text]', d).textContent = clashes.length === 1
      ? '„' + clashes[0] + '“ gibt es in diesem Ordner schon.'
      : clashes.length + ' Namen gibt es in diesem Ordner schon:';
    var list = $('[data-list]', d);
    list.innerHTML = '';
    clashes.slice(0, 8).forEach(function (n) { list.appendChild(el('li', '', n)); });
    if (clashes.length > 8) list.appendChild(el('li', 'muted', '… und ' + (clashes.length - 8) + ' weitere'));
    d.returnValue = '';
    d.onclose = function () {
      if (d.returnValue === 'replace') runUpload(items);
      if (d.returnValue === 'skip') {
        runUpload(items.filter(function (it) { return clashes.indexOf(it.rel.split('/')[0]) === -1; }));
      }
    };
    d.showModal();
  }

  function runUpload(items) {
    if (items.length === 0) return;
    var total = items.reduce(function (s, it) { return s + it.file.size; }, 0) || 1;
    var doneBytes = 0, i = 0, errors = [];
    var bar = $('[data-up-bar]', uploadsBox), count = $('[data-up-count]', uploadsBox);
    var title = $('[data-up-title]', uploadsBox), errBox = $('[data-up-err]', uploadsBox);
    uploadsBox.hidden = false;
    errBox.hidden = true;
    title.textContent = 'Hochladen …';

    function next() {
      if (i >= items.length) {
        title.textContent = errors.length ? 'Hochladen mit Fehlern beendet' : 'Hochladen fertig';
        bar.style.width = '100%';
        if (errors.length) {
          errBox.hidden = false;
          errBox.textContent = errors.join('\n');
          setTimeout(function () { location.reload(); }, 4000);
        } else {
          location.reload();
        }
        return;
      }
      var it = items[i];
      count.textContent = (i + 1) + ' / ' + items.length;
      var data = new FormData();
      data.append('csrf', csrf());
      data.append('do', 'upload');
      data.append('dir', curDir);
      data.append('relpath', it.rel);
      data.append('file', it.file, it.file.name);
      var xhr = new XMLHttpRequest();
      xhr.open('POST', '/web.php');
      xhr.setRequestHeader('X-Requested-With', 'fetch');
      xhr.upload.onprogress = function (e) {
        bar.style.width = Math.min(100, (doneBytes + e.loaded) * 100 / total) + '%';
      };
      xhr.onload = function () {
        var res = null;
        try { res = JSON.parse(xhr.responseText); } catch (e) { /* kein JSON */ }
        if (!res || res.status !== 'ok') {
          errors.push(res && res.message ? res.message : it.rel + ': Fehler ' + xhr.status);
        }
        doneBytes += it.file.size; i++; next();
      };
      xhr.onerror = function () { errors.push(it.rel + ': Verbindung unterbrochen'); doneBytes += it.file.size; i++; next(); };
      xhr.send(data);
    }
    next();
  }

  var fileInput = document.getElementById('fileInput');
  var folderInput = document.getElementById('folderInput');
  if (fileInput) {
    fileInput.addEventListener('change', function () {
      startUpload(Array.prototype.map.call(fileInput.files, function (f) { return { file: f, rel: f.name }; }));
      fileInput.value = '';
    });
    folderInput.addEventListener('change', function () {
      startUpload(Array.prototype.map.call(folderInput.files, function (f) {
        return { file: f, rel: f.webkitRelativePath || f.name };
      }));
      folderInput.value = '';
    });
  }

  /* Drag & Drop inkl. Ordnern (webkitGetAsEntry) */
  var dropzone = $('[data-dropzone]');
  if (dropzone) {
    var wrap = $('.dropzone-wrap');
    var depth = 0;
    var hasFiles = function (ev) { return ev.dataTransfer && Array.prototype.indexOf.call(ev.dataTransfer.types, 'Files') !== -1; };
    document.addEventListener('dragenter', function (ev) { if (hasFiles(ev)) { depth++; dropzone.hidden = false; wrap.classList.add('dragging'); } });
    document.addEventListener('dragleave', function () { if (--depth <= 0) { depth = 0; dropzone.hidden = true; wrap.classList.remove('dragging'); } });
    document.addEventListener('dragover', function (ev) { if (hasFiles(ev)) ev.preventDefault(); });
    document.addEventListener('drop', function (ev) {
      if (!hasFiles(ev)) return;
      ev.preventDefault();
      depth = 0; dropzone.hidden = true; wrap.classList.remove('dragging');
      var entries = Array.prototype.map.call(ev.dataTransfer.items, function (it) {
        return it.webkitGetAsEntry ? it.webkitGetAsEntry() : null;
      }).filter(Boolean);
      if (entries.length === 0) {
        startUpload(Array.prototype.map.call(ev.dataTransfer.files, function (f) { return { file: f, rel: f.name }; }));
        return;
      }
      collect(entries).then(startUpload);
    });
  }

  function collect(entries) {
    var out = [];
    function walk(entry, prefix) {
      return new Promise(function (resolve) {
        if (entry.isFile) {
          entry.file(function (f) { out.push({ file: f, rel: prefix + f.name }); resolve(); }, function () { resolve(); });
        } else if (entry.isDirectory) {
          var reader = entry.createReader(), all = [];
          var read = function () {
            reader.readEntries(function (batch) {
              if (batch.length === 0) {
                Promise.all(all.map(function (e) { return walk(e, prefix + entry.name + '/'); })).then(resolve);
              } else {
                all = all.concat(Array.prototype.slice.call(batch));
                read();
              }
            }, function () { resolve(); });
          };
          read();
        } else {
          resolve();
        }
      });
    }
    return Promise.all(entries.map(function (e) { return walk(e, ''); })).then(function () { return out; });
  }
})();
