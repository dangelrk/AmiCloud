/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#include "mainwindow.h"
#include "panelwidget.h"
#include "infodialog.h"
#include "searchdialog.h"
#include "searchresultswindow.h"

extern "C" {
#include "localfs.h"
#include "ui_hooks.h"
#include "md5.h"
#include "cmd.h"
#include "search.h"
}

#include <QSplitter>
#include <QLabel>
#include <QStatusBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QProgressDialog>
#include <QKeyEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QApplication>
#include <cstring>

MainWindow::MainWindow(const AcConfig &cfg, QWidget *parent)
    : QMainWindow(parent), m_cfg(cfg)
{
    setWindowTitle(tr("AmiCloud Commander"));
    resize(1000, 600);

    m_left  = new PanelWidget(this);
    m_right = new PanelWidget(this);
    m_left->setConfig(m_cfg);
    m_right->setConfig(m_cfg);

    char localCwd[AC_MAX_PATH];
    QString startLocal = localfs_getcwd(localCwd, sizeof(localCwd)) ? QString::fromUtf8(localCwd) : QStringLiteral(".");
    m_left->initLocal(startLocal);
    m_right->initCloud(QString::fromUtf8(m_cfg.basepath[0] ? m_cfg.basepath : "/"));

    m_active = m_left;
    m_left->setActive(true);

    connect(m_left, &PanelWidget::activated, this, &MainWindow::onPanelActivated);
    connect(m_right, &PanelWidget::activated, this, &MainWindow::onPanelActivated);
    connect(m_left, &PanelWidget::busy, this, &MainWindow::setBusy);
    connect(m_right, &PanelWidget::busy, this, &MainWindow::setBusy);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(m_left);
    splitter->addWidget(m_right);

    /* Button row mirroring the Amiga/MUI Commander's F2..F10 buttons
     * (apps/amicloud-commander-mui/src/gui.c) — until now this was keyboard-only, discoverable
     * just from the status bar hint text below. */
    auto *buttonRow = new QHBoxLayout;
    auto addButton = [&](const QString &label, void (MainWindow::*slot)()) {
        auto *b = new QPushButton(label, this);
        connect(b, &QPushButton::clicked, this, slot);
        buttonRow->addWidget(b);
    };
    addButton(tr("F2 Neu laden"), &MainWindow::doReload);
    addButton(tr("F3 Info"), &MainWindow::doInfo);
    addButton(tr("F5 Kopieren"), &MainWindow::doCopy);
    addButton(tr("F6 Umben./Versch."), &MainWindow::doRenameMove);
    addButton(tr("F7 MkDir"), &MainWindow::doMkdir);
    addButton(tr("F8 Loeschen"), &MainWindow::doDelete);
    addButton(tr("F4 Suchen"), &MainWindow::doSearch);
    auto *btnQuit = new QPushButton(tr("F10 Beenden"), this);
    connect(btnQuit, &QPushButton::clicked, this, &QWidget::close);
    buttonRow->addWidget(btnQuit);

    auto *central = new QWidget(this);
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->addWidget(splitter, 1);
    centralLayout->addLayout(buttonRow);
    setCentralWidget(central);

    auto *viewMenu = menuBar()->addMenu(tr("&Ansicht"));
    m_hideHiddenAction = viewMenu->addAction(tr("Versteckte Dateien ausblenden"));
    m_hideHiddenAction->setCheckable(true);
    connect(m_hideHiddenAction, &QAction::toggled, this, [this](bool checked) {
        m_left->setHideHidden(checked);
        m_right->setHideHidden(checked);
    });

    auto *extrasMenu = menuBar()->addMenu(tr("&Extras"));
    QAction *checksumAction = extrasMenu->addAction(tr("Pruefsumme anzeigen"));
    connect(checksumAction, &QAction::triggered, this, &MainWindow::doChecksum);
    QAction *searchAction = extrasMenu->addAction(tr("Suchen..."));
    connect(searchAction, &QAction::triggered, this, &MainWindow::doSearch);

    auto *helpMenu = menuBar()->addMenu(tr("&Hilfe"));
    QAction *aboutAction = helpMenu->addAction(tr("Ueber AmiCloud Commander..."));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::doAbout);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->showMessage(tr("F3 Info  F5 Kopieren  F6 Umben./Versch.  F7 MkDir  F8 Loeschen  F10 Beenden"));

    /* Hidden by default; shown for the duration of any Cloud round-trip
     * that runs on the GUI thread (see PanelWidget::reload(), doInfo()
     * below) alongside the wait cursor — same purpose as gui_busy_begin()/
     * _end() in the Amiga frontend's main window. */
    m_busyLabel = new QLabel(this);
    m_busyLabel->hide();
    statusBar()->addPermanentWidget(m_busyLabel);
}

void MainWindow::setBusy(bool on)
{
    if (on) {
        if (m_busyCount++ == 0) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            m_busyLabel->setText(tr("⏳ Online-Zugriff..."));
            m_busyLabel->show();
        }
    } else if (m_busyCount > 0 && --m_busyCount == 0) {
        QApplication::restoreOverrideCursor();
        m_busyLabel->hide();
    }
}

void MainWindow::onPanelActivated(PanelWidget *which)
{
    if (which == m_active) return;
    m_active = which;
    m_left->setActive(m_left == m_active);
    m_right->setActive(m_right == m_active);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Tab:
        otherPanel()->setFocus();
        return;
    case Qt::Key_F2:
        doReload();
        return;
    case Qt::Key_F3:
        doInfo();
        return;
    case Qt::Key_F4:
        doSearch();
        return;
    case Qt::Key_F5:
        doCopy();
        return;
    case Qt::Key_F6:
        doRenameMove();
        return;
    case Qt::Key_F7:
        doMkdir();
        return;
    case Qt::Key_F8:
    case Qt::Key_Delete:
        doDelete();
        return;
    case Qt::Key_F10:
    case Qt::Key_Escape:
        close();
        return;
    default:
        break;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::doReload()
{
    activePanel()->reload();
}

void MainWindow::doInfo()
{
    PanelWidget *p = activePanel();
    QVector<PanelEntry> entries = p->targetEntries();
    if (entries.isEmpty()) return;

    bool cloud = (p->source() == PANEL_CLOUD);
    if (cloud) setBusy(true);
    InfoDialog::showFor(p->source(), m_cfg, p->childPath(entries.first()), entries.first(), this);
    if (cloud) setBusy(false);
}

void MainWindow::doCopy()
{
    PanelWidget *src = activePanel();
    PanelWidget *dst = otherPanel();
    QVector<PanelEntry> entries = src->targetEntries();
    if (entries.isEmpty()) return;

    if (src->source() == PANEL_CLOUD && dst->source() == PANEL_CLOUD) {
        QMessageBox::information(this, tr("Kopieren"),
            tr("Cloud->Cloud-Kopieren wird noch nicht unterstuetzt (siehe Plan Abschnitt 3.3)."));
        return;
    }

    TransferJob::Op op;
    if (src->source() == PANEL_LOCAL && dst->source() == PANEL_CLOUD)      op = TransferJob::CopyLocalToCloud;
    else if (src->source() == PANEL_CLOUD && dst->source() == PANEL_LOCAL) op = TransferJob::CopyCloudToLocal;
    else                                                                  op = TransferJob::CopyLocalToLocal;

    /* No per-file overwrite pre-check and no separate "N Objekt(e)
     * kopieren?" confirmation here anymore (copy-flow rework) — the
     * existence check and overwrite decision now happen inline, per
     * conflict, inside TransferJob (CopyCtx/panel_copy_file(), see
     * panel.h), with Overwrite/Skip/Alle.../Abbruch options and both
     * files' details, exactly when a conflict is actually found. That
     * separate confirmation only made sense once, up front, for
     * Verschieben (doRenameMove() below) — a copy just starts. dst's
     * listing is already loaded (this panel), so findEntry() below costs
     * no extra round-trip, unlike the old per-file cmd_exists() probe. */
    QVector<TransferJob::Item> items;
    for (const PanelEntry &e : entries) {
        TransferJob::Item item;
        item.src   = src->childPath(e);
        item.dst   = dst->childPath(e);
        item.isDir = e.is_dir;
        item.name  = QString::fromUtf8(e.name);
        if (!e.is_dir) {
            item.smeta.size = e.size;
            std::strncpy(item.smeta.date, e.modified, sizeof(item.smeta.date) - 1);
            std::strncpy(item.smeta.md5, e.md5, sizeof(item.smeta.md5) - 1);
            if (const PanelEntry *d = dst->findEntry(item.name)) {
                item.hasDst = true;
                item.dmeta.size = d->size;
                std::strncpy(item.dmeta.date, d->modified, sizeof(item.dmeta.date) - 1);
                std::strncpy(item.dmeta.md5, d->md5, sizeof(item.dmeta.md5) - 1);
            }
        }
        items.push_back(item);
    }

    startJob(op, items);
}

void MainWindow::doRenameMove()
{
    PanelWidget *src = activePanel();
    PanelWidget *dst = otherPanel();
    QVector<PanelEntry> entries = src->targetEntries();
    if (entries.isEmpty()) return;

    if (src->source() != dst->source()) {
        /* Cross-panel: Local<->Cloud move = copy + delete source. */
        if (QMessageBox::question(this, tr("Verschieben"),
                tr("%1 Objekt(e) nach %2 verschieben?").arg(entries.size()).arg(dst->path()))
            != QMessageBox::Yes)
            return;

        QVector<TransferJob::Item> items;
        for (const PanelEntry &e : entries)
            items.push_back({src->childPath(e), dst->childPath(e), bool(e.is_dir)});

        TransferJob::Op op = (src->source() == PANEL_LOCAL) ? TransferJob::MoveLocalToCloud
                                                            : TransferJob::MoveCloudToLocal;
        startJob(op, items);
        return;
    }

    /* Same source on both sides: plain rename in place. */
    if (entries.size() != 1) {
        QMessageBox::information(this, tr("Umbenennen"), tr("Bitte genau ein Objekt auswaehlen."));
        return;
    }
    const PanelEntry &e = entries.first();
    bool ok = false;
    QString newName = QInputDialog::getText(this, tr("Umbenennen"), tr("Neuer Name:"),
                                            QLineEdit::Normal, QString::fromUtf8(e.name), &ok);
    if (!ok || newName.isEmpty() || newName == QString::fromUtf8(e.name)) return;

    QString oldPath = src->childPath(e);
    PanelEntry renamed = e;
    QByteArray nameUtf8 = newName.toUtf8();
    std::strncpy(renamed.name, nameUtf8.constData(), sizeof(renamed.name) - 1);
    renamed.name[sizeof(renamed.name) - 1] = '\0';
    QString newPath = src->childPath(renamed);

    if (src->source() == PANEL_CLOUD) {
        QVector<TransferJob::Item> items{ {oldPath, newPath, bool(e.is_dir)} };
        startJob(TransferJob::RenameCloud, items);
    } else {
        if (QFileInfo::exists(newPath)) {
            QMessageBox::warning(this, tr("Umbenennen"), tr("Ziel existiert bereits."));
            return;
        }
        QDir().rename(oldPath, newPath);
        src->reload();
    }
}

void MainWindow::doMkdir()
{
    PanelWidget *p = activePanel();
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Verzeichnis anlegen"), tr("Name:"),
                                         QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty()) return;

    PanelEntry tmp;
    std::memset(&tmp, 0, sizeof(tmp));
    QByteArray nameUtf8 = name.toUtf8();
    std::strncpy(tmp.name, nameUtf8.constData(), sizeof(tmp.name) - 1);
    QString path = p->childPath(tmp);

    if (p->source() == PANEL_CLOUD) {
        QVector<TransferJob::Item> items{ {path, QString(), true} };
        startJob(TransferJob::MkdirCloud, items);
    } else {
        QDir().mkpath(path);
        p->reload();
    }
}

void MainWindow::doDelete()
{
    PanelWidget *p = activePanel();
    QVector<PanelEntry> entries = p->targetEntries();
    if (entries.isEmpty()) return;

    if (QMessageBox::question(this, tr("Loeschen"),
            tr("%1 Objekt(e) endgueltig loeschen?").arg(entries.size()))
        != QMessageBox::Yes)
        return;

    QVector<TransferJob::Item> items;
    for (const PanelEntry &e : entries)
        items.push_back({p->childPath(e), QString(), bool(e.is_dir)});

    TransferJob::Op op = (p->source() == PANEL_CLOUD) ? TransferJob::DeleteCloud : TransferJob::DeleteLocal;
    startJob(op, items);
}

/* Pruefsumme anzeigen — selection in the active panel, up to 20 lines.
 * Cloud entries already carry their MD5 from the listing; local entries
 * get it computed on demand (md5_file_hex(), same helper the shell's
 * lmd5 command uses). */
void MainWindow::doChecksum()
{
    PanelWidget *p = activePanel();
    QVector<PanelEntry> entries = p->targetEntries();
    if (entries.isEmpty()) return;

    QString text;
    int shown = 0, extra = 0;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const PanelEntry &e : entries) {
        QString line;
        if (e.is_dir) {
            line = tr("%1: (Verzeichnis)").arg(QString::fromUtf8(e.name));
        } else if (p->source() == PANEL_CLOUD) {
            line = tr("%1: %2").arg(QString::fromUtf8(e.name),
                      e.md5[0] ? QString::fromUtf8(e.md5) : tr("(unbekannt)"));
        } else {
            char hex[33];
            QByteArray path = p->childPath(e).toUtf8();
            line = md5_file_hex(path.constData(), hex)
                 ? tr("%1: %2").arg(QString::fromUtf8(e.name), QString::fromUtf8(hex))
                 : tr("%1: (Fehler)").arg(QString::fromUtf8(e.name));
        }
        if (shown++ < 20) text += line + "\n";
        else extra++;
    }
    QApplication::restoreOverrideCursor();

    if (extra) text += tr("... und %1 weitere").arg(extra);
    QMessageBox::information(this, tr("Pruefsumme"), text);
}

/* F4/Suchen (docs/design/search-plan.md section 5). Cloud panel:
 * one cmd_search() round-trip. Local panel: search_local()'s recursive
 * walk, filtered for dirsToo() here (search_local() itself reports every
 * match, cheaper to filter the small in-memory result than to teach the
 * walker about the flag). Either way, results land in the single reused
 * SearchResultsWindow instance. */
void MainWindow::doSearch()
{
    PanelWidget *launchPanel = activePanel();
    bool cloud = (launchPanel->source() == PANEL_CLOUD);

    SearchDialog dlg(cloud, this);
    if (dlg.exec() != QDialog::Accepted) return;

    QString pattern = dlg.pattern();
    if (pattern.isEmpty()) return;

    m_searchLaunchPanel = launchPanel;

    QVector<SearchHit> hits;
    bool truncated = false;

    if (cloud) {
        QString scope = dlg.wholeScope() ? QStringLiteral("/") : launchPanel->path();
        QByteArray scopeUtf8 = scope.toUtf8();
        QByteArray patUtf8   = pattern.toUtf8();
        AcSearchHit *raw = nullptr;
        int n = 0, trunc = 0;

        setBusy(true);
        bool ok = cmd_search(&m_cfg, scopeUtf8.constData(), patUtf8.constData(),
                             dlg.dirsToo() ? 1 : 0, &raw, &n, &trunc) != 0;
        setBusy(false);

        if (ok) {
            hits.reserve(n);
            for (int i = 0; i < n; i++) {
                SearchHit h;
                std::strncpy(h.path, raw[i].path, sizeof(h.path) - 1);
                h.path[sizeof(h.path) - 1] = '\0';
                h.is_dir = raw[i].is_dir;
                h.size   = raw[i].size;
                h.mtime  = raw[i].mtime;
                hits.append(h);
            }
            cmd_search_free(raw);
            truncated = (trunc != 0);
        }
    } else {
        QString scope = dlg.wholeScope() ? QDir(launchPanel->path()).rootPath() : launchPanel->path();
        QByteArray scopeUtf8 = scope.toUtf8();
        QByteArray patUtf8   = pattern.toUtf8();
        struct Ctx { QVector<SearchHit> *hits; bool dirsToo; };
        Ctx ctx{ &hits, dlg.dirsToo() };

        QApplication::setOverrideCursor(Qt::WaitCursor);
        search_local(scopeUtf8.constData(), patUtf8.constData(),
            [](const SearchHit *hit, void *ud) {
                auto *c = static_cast<Ctx *>(ud);
                if (hit->is_dir && !c->dirsToo) return;
                c->hits->append(*hit);
            }, &ctx);
        QApplication::restoreOverrideCursor();
    }

    if (!m_searchResults) {
        m_searchResults = new SearchResultsWindow(this);
        connect(m_searchResults, &SearchResultsWindow::activated, this, &MainWindow::gotoSearchHit);
        connect(m_searchResults, &QObject::destroyed, this, [this]() { m_searchResults = nullptr; });
    }
    m_searchResults->setResults(hits, truncated);
    m_searchResults->show();
    m_searchResults->raise();
    m_searchResults->activateWindow();
}

/* Doppelklick/Enter/"Gehe zu" in the results window: split the hit's full
 * path into directory+basename and let the launching panel jump there
 * (PanelWidget::revealEntry()) -- not necessarily the currently active
 * panel, since focus may have moved to the results window since. */
void MainWindow::gotoSearchHit(const SearchHit &hit)
{
    if (!m_searchLaunchPanel) return;

    QString path = QString::fromUtf8(hit.path);
    int cut = qMax(path.lastIndexOf('/'), path.lastIndexOf('\\'));
    QString dir  = (cut >= 0) ? path.left(cut) : QString();
    QString name = (cut >= 0) ? path.mid(cut + 1) : path;
    if (m_searchLaunchPanel->source() == PANEL_CLOUD && dir.isEmpty()) dir = "/";

    m_searchLaunchPanel->revealEntry(dir, name);
    raise();
    activateWindow();
}

void MainWindow::doAbout()
{
    QMessageBox::information(this, tr("Ueber AmiCloud Commander"),
        tr("AmiCloud Commander\nDual-pane AmiCloud file manager\n\n"
           "Version " AC_VERSION "\n(C) 2026 Ronny Krueger\n"
           "GNU General Public License v3 or later"));
}

void MainWindow::startJob(TransferJob::Op op, QVector<TransferJob::Item> items)
{
    if (m_job) return; /* one worker at a time, per plan section 6.3 */

    m_progressDlg = new QProgressDialog(tr("Uebertrage..."), tr("Abbrechen"), 0, 100, this);
    m_progressDlg->setWindowModality(Qt::WindowModal);
    m_progressDlg->setMinimumDuration(300);

    m_job = new TransferJob(op, m_cfg, items, this);

    /* Overall x/y file-count progress (CopyLocalToCloud/CopyCloudToLocal
     * only, see TransferJob::hookProgress()) — the bar tracks files done,
     * not bytes of the current file, and the label shows "(x/y) name" so
     * the count is visible right where Abbrechen is, not just in the
     * status bar (copy-flow rework). */
    connect(m_job, &TransferJob::overallProgressChanged, this,
            [this](qint64 bytesDone, qint64 bytesTotal, int fileDone, int fileTotal, QString name, long bps) {
        if (!m_progressDlg) return;
        QString label = tr("(%1/%2) %3").arg(fileDone).arg(fileTotal).arg(name);
        if (bps > 0) label += tr("  – %1 KB/s").arg(bps / 1024);
        if (bytesTotal > 0) {
            m_progressDlg->setMaximum(100);
            m_progressDlg->setValue(int(bytesDone * 100 / bytesTotal));
            m_progressDlg->setLabelText(label);
        } else {
            m_progressDlg->setMaximum(0); /* indeterminate: scan phase not finished yet */
            if (!name.isEmpty()) m_progressDlg->setLabelText(label);
        }
    });
    /* Every other Op (Move/Rename/Delete/Mkdir/CopyLocalToLocal) keeps the
     * old current-file byte-percentage display, now with bps attached. */
    connect(m_job, &TransferJob::progressChanged, this,
            [this](qint64 done, qint64 total, QString name, long bps) {
        if (!m_progressDlg) return;
        if (!name.isEmpty()) {
            QString label = name;
            if (bps > 0) label += tr("  – %1 KB/s").arg(bps / 1024);
            m_progressDlg->setLabelText(label);
        }
        if (total > 0) {
            m_progressDlg->setMaximum(100);
            m_progressDlg->setValue(int(done * 100 / total));
        } else {
            m_progressDlg->setMaximum(0); /* indeterminate */
        }
    });
    connect(m_progressDlg, &QProgressDialog::canceled, m_job, &TransferJob::requestCancel);
    connect(m_job, &TransferJob::logMessage, this, [this](int level, QString msg) {
        m_statusLabel->setText(msg);
        Q_UNUSED(level);
    });
    connect(m_job, &TransferJob::jobFinished, this,
            [this](int okCount, int errCount, bool cancelled) {
        m_progressDlg->close();
        m_progressDlg->deleteLater();
        m_progressDlg = nullptr;
        m_job->deleteLater();
        m_job = nullptr;
        m_left->reload();
        m_right->reload();
        m_statusLabel->setText(cancelled ? tr("Abgebrochen")
                                        : tr("%1 ok, %2 Fehler").arg(okCount).arg(errCount));
    });

    m_job->start();
}
