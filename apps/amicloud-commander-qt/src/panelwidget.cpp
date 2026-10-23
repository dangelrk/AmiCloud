/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#include "panelwidget.h"
#include "paneltablemodel.h"

extern "C" {
#include "localfs.h"
#include "ui_hooks.h"
}

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QLabel>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QDir>
#include <cstring>

PanelWidget::PanelWidget(QWidget *parent)
    : QWidget(parent)
{
    m_sourceCombo = new QComboBox(this);
    m_sourceCombo->addItem(tr("Local"));
    m_sourceCombo->addItem(tr("AmiCloud"));

    m_pathEdit = new QLineEdit(this);

    /* Matches the Amiga/MUI Commander's per-panel ".. (rauf)" button
     * (apps/amicloud-commander-mui/src/panel_mui.c) — goUp() was previously reachable only via
     * Backspace, undiscoverable for a mouse-only user. */
    m_upButton = new QPushButton(tr(".. (rauf)"), this);

    m_model = new PanelTableModel(this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(PanelTableModel::ColName, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(false); /* dirs-first sort comes from panel_sort(), see reload() */

    m_statusLabel = new QLabel(this);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_sourceCombo);
    topRow->addWidget(m_pathEdit, 1);
    topRow->addWidget(m_upButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_statusLabel);

    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PanelWidget::onSourceChanged);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &PanelWidget::onPathEditReturn);
    connect(m_upButton, &QPushButton::clicked, this, &PanelWidget::goUp);
    connect(m_table, &QTableView::doubleClicked, this, &PanelWidget::onDoubleClicked);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PanelWidget::onSelectionChanged);

    m_table->installEventFilter(this);
    setFocusProxy(m_table);
}

bool PanelWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_table) {
        if (event->type() == QEvent::FocusIn) {
            emit activated(this);
        } else if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            switch (ke->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                enterSelected();
                return true;
            case Qt::Key_Backspace:
                goUp();
                return true;
            case Qt::Key_Space:
            case Qt::Key_Insert: {
                QModelIndex idx = m_table->currentIndex();
                if (idx.isValid())
                    m_table->selectionModel()->select(
                        idx, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
                int nextRow = qMin(idx.row() + 1, m_model->rowCount() - 1);
                m_table->setCurrentIndex(m_model->index(nextRow, 0));
                return true;
            }
            default:
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PanelWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    emit activated(this);
}

void PanelWidget::setConfig(const AcConfig &cfg)
{
    m_cfg = cfg;
}

void PanelWidget::initLocal(const QString &startPath)
{
    m_source = PANEL_LOCAL;
    QSignalBlocker blocker(m_sourceCombo);
    m_sourceCombo->setCurrentIndex(0);
    applyPathAndReload(startPath);
}

void PanelWidget::initCloud(const QString &startPath)
{
    m_source = PANEL_CLOUD;
    QSignalBlocker blocker(m_sourceCombo);
    m_sourceCombo->setCurrentIndex(1);
    applyPathAndReload(startPath.isEmpty() ? "/" : startPath);
}

/* Remembers each source's last path across switches — flipping the combo
 * box back and forth shouldn't discard where you'd navigated to. */
void PanelWidget::onSourceChanged(int idx)
{
    m_source = (idx == 1) ? PANEL_CLOUD : PANEL_LOCAL;
    if (m_source == PANEL_LOCAL) {
        if (m_localPath.isEmpty()) {
            char buf[AC_MAX_PATH];
            m_localPath = localfs_getcwd(buf, sizeof(buf)) ? QString::fromUtf8(buf) : QStringLiteral("/");
        }
        applyPathAndReload(m_localPath);
    } else {
        if (m_cloudPath.isEmpty())
            m_cloudPath = QString::fromUtf8(m_cfg.basepath[0] ? m_cfg.basepath : "/");
        applyPathAndReload(m_cloudPath);
    }
}

void PanelWidget::applyPathAndReload(const QString &path)
{
    m_path = path;
    if (m_source == PANEL_LOCAL) m_localPath = path;
    else                         m_cloudPath = path;
    m_pathEdit->setText(path);
    reload();
}

void PanelWidget::navigateTo(const QString &path)
{
    applyPathAndReload(path);
}

void PanelWidget::onPathEditReturn()
{
    navigateTo(m_pathEdit->text());
}

void PanelWidget::reload()
{
    PanelList list;
    QByteArray pathUtf8 = m_path.toUtf8();
    bool cloud = (m_source == PANEL_CLOUD);

    /* Cloud listing is a synchronous HTTP round-trip on the GUI thread
     * (unlike F5/F6/F7/F8, it isn't off-loaded to TransferJob) — without
     * this signal the window just freezes with no cursor/indicator change
     * for however long that takes. Local listing is effectively instant,
     * no need to flag it busy. See MainWindow::setBusy(). */
    if (cloud) emit busy(true);
    bool ok = panel_read(m_source, &m_cfg, pathUtf8.constData(), &list);
    if (cloud) emit busy(false);

    if (!ok) {
        m_model->setList(PanelList{nullptr, 0}, m_path != "/");
        setStatusText(tr("Fehler beim Lesen von %1").arg(m_path));
        return;
    }
    panel_sort(&list, PANEL_SORT_NAME);

    if (m_source == PANEL_LOCAL && m_hideHidden) {
        int kept = 0;
        for (int i = 0; i < list.n; i++) {
            if (list.items[i].name[0] == '.') continue;
            if (kept != i) list.items[kept] = list.items[i];
            kept++;
        }
        list.n = kept;
    }

    bool showUp = (m_source == PANEL_CLOUD) ? (m_path != "/") : true;
    m_model->setList(list, showUp);
    setStatusText(tr("%1 Objekt(e)").arg(list.n));
}

void PanelWidget::setHideHidden(bool hide)
{
    if (m_hideHidden == hide) return;
    m_hideHidden = hide;
    reload();
}

void PanelWidget::enterSelected()
{
    QModelIndexList sel = m_table->selectionModel()->selectedRows();
    int row = sel.isEmpty() ? m_table->currentIndex().row() : sel.first().row();
    if (row < 0) return;

    if (m_model->isUpRow(row)) { goUp(); return; }

    const PanelEntry *e = m_model->entryAt(row);
    if (e && e->is_dir) navigateTo(childPath(*e));
}

void PanelWidget::onDoubleClicked(const QModelIndex &index)
{
    if (m_model->isUpRow(index.row())) { goUp(); return; }
    const PanelEntry *e = m_model->entryAt(index.row());
    if (e && e->is_dir) navigateTo(childPath(*e));
}

void PanelWidget::goUp()
{
    if (m_source == PANEL_CLOUD) {
        char resolved[AC_MAX_PATH];
        remote_resolve(resolved, sizeof(resolved), m_path.toUtf8().constData(), "..");
        navigateTo(QString::fromUtf8(resolved));
    } else {
        /* QDir rather than localfs_chdir("..")+getcwd() round-tripping:
         * that would mutate the whole process's current directory, which
         * a second Local panel (or a concurrently running TransferJob)
         * has no reason to expect. */
        QDir dir(m_path);
        if (dir.cdUp()) navigateTo(dir.absolutePath());
    }
}

QString PanelWidget::childPath(const PanelEntry &e) const
{
    if (m_source == PANEL_CLOUD) {
        QString base = m_path;
        if (!base.endsWith('/')) base += '/';
        return base + QString::fromUtf8(e.name);
    }
    char out[AC_MAX_PATH];
    QByteArray dir = m_path.toUtf8();
    localfs_join(out, sizeof(out), dir.constData(), e.name);
    return QString::fromUtf8(out);
}

const PanelEntry *PanelWidget::findEntry(const QString &name) const
{
    return m_model->entryByName(name);
}

void PanelWidget::revealEntry(const QString &dir, const QString &name)
{
    navigateTo(dir);
    int row = m_model->rowForName(name);
    if (row < 0) return;
    QModelIndex idx = m_model->index(row, 0);
    m_table->setCurrentIndex(idx);
    m_table->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    m_table->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    setFocus();
}

QVector<PanelEntry> PanelWidget::targetEntries() const
{
    QVector<PanelEntry> result;
    QModelIndexList sel = m_table->selectionModel()->selectedRows();
    if (!sel.isEmpty()) {
        for (const QModelIndex &idx : sel) {
            const PanelEntry *e = m_model->entryAt(idx.row());
            if (e) result.append(*e);
        }
        return result;
    }
    int row = m_table->currentIndex().row();
    const PanelEntry *e = m_model->entryAt(row);
    if (e) result.append(*e);
    return result;
}

void PanelWidget::onSelectionChanged()
{
    QModelIndexList sel = m_table->selectionModel()->selectedRows();
    if (sel.isEmpty()) {
        reload(); /* cheap; clears any stale "marked" highlighting */
        return;
    }
    long total = 0;
    for (const QModelIndex &idx : sel) {
        const PanelEntry *e = m_model->entryAt(idx.row());
        if (e && !e->is_dir) total += e->size;
    }
    setStatusText(tr("%1 markiert, %2 Bytes").arg(sel.size()).arg(total));
}

void PanelWidget::setActive(bool active)
{
    m_active = active;
    setStyleSheet(active ? "PanelWidget { border: 2px solid #3070c0; }" : "");
}

void PanelWidget::setStatusText(const QString &text)
{
    m_statusLabel->setText(text);
}
