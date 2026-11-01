// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "foldertree.h"
#include "synccontroller.h"

#include <QApplication>
#include <QHeaderView>
#include <QStyle>

enum { RelRole = Qt::UserRole + 1, LoadedRole, PlaceholderRole };

static bool isBelow(const QString &path, const QString &prefix)
{
    if (prefix.isEmpty()) return !path.isEmpty();
    return path.size() > prefix.size() && path.startsWith(prefix) &&
           path.at(prefix.size()) == QLatin1Char('/');
}

FolderTree::FolderTree(SyncController *ctl, QWidget *parent)
    : QTreeWidget(parent), m_ctl(ctl)
{
    setHeaderHidden(true);
    setColumnCount(1);
    setUniformRowHeights(true);
    connect(this, &QTreeWidget::itemExpanded, this, &FolderTree::onExpanded);
    connect(this, &QTreeWidget::itemChanged, this, &FolderTree::onChanged);
    connect(m_ctl, &SyncController::foldersListed, this, &FolderTree::onListed);
}

QString FolderTree::absPath(const QString &rel) const
{
    if (rel.isEmpty()) return m_root;
    return m_root == QLatin1String("/") ? QLatin1Char('/') + rel : m_root + QLatin1Char('/') + rel;
}

void FolderTree::load(const Account &acc, const QString &remoteRoot, const QStringList &excludes)
{
    m_acc = acc;
    m_root = remoteRoot;
    m_excludes = excludes;
    normalize();
    m_pending.clear();
    m_updating = true;
    clear();
    auto *root = new QTreeWidgetItem(this);
    root->setText(0, remoteRoot == QLatin1String("/") ? tr("Alles auf dem Server (/)") : remoteRoot);
    root->setIcon(0, style()->standardIcon(QStyle::SP_DriveNetIcon));
    root->setData(0, RelRole, QString());
    root->setFlags(Qt::ItemIsEnabled);   // the start folder itself is always synced
    m_updating = false;
    request(root);
    root->setExpanded(true);
}

QTreeWidgetItem *FolderTree::addFolder(QTreeWidgetItem *parent, const QString &name,
                                       const QString &rel)
{
    auto *it = new QTreeWidgetItem(parent);
    it->setText(0, name);
    it->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
    it->setData(0, RelRole, rel);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    it->setCheckState(0, stateOf(rel));
    // placeholder until opened: every folder may have subfolders
    auto *ph = new QTreeWidgetItem(it);
    ph->setText(0, tr("…"));
    ph->setData(0, PlaceholderRole, true);
    ph->setFlags(Qt::NoItemFlags);
    return it;
}

QTreeWidgetItem *FolderTree::findItem(const QString &rel) const
{
    QTreeWidgetItem *it = topLevelItem(0);
    if (!it || rel.isEmpty()) return it;
    const QStringList parts = rel.split(QLatin1Char('/'));
    QString cur;
    for (const QString &p : parts) {
        cur = cur.isEmpty() ? p : cur + QLatin1Char('/') + p;
        QTreeWidgetItem *next = nullptr;
        for (int i = 0; i < it->childCount(); i++)
            if (it->child(i)->data(0, RelRole).toString() == cur) { next = it->child(i); break; }
        if (!next) return nullptr;
        it = next;
    }
    return it;
}

void FolderTree::request(QTreeWidgetItem *item)
{
    if (item->data(0, LoadedRole).toBool()) return;
    QString rel = item->data(0, RelRole).toString();
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        if (it.value() == rel) return;   // already asked
    int id = m_ctl->listFolders(m_acc, absPath(rel));
    m_pending.insert(id, rel);
}

void FolderTree::onExpanded(QTreeWidgetItem *item)
{
    if (!m_updating) request(item);
}

void FolderTree::onListed(int reqId, const QString &, bool ok, int httpStatus,
                          const QStringList &folders)
{
    auto pit = m_pending.find(reqId);
    if (pit == m_pending.end()) return;   // not ours (or an old tree)
    QString rel = pit.value();
    m_pending.erase(pit);
    QTreeWidgetItem *item = findItem(rel);
    if (!item) return;

    m_updating = true;
    for (int i = item->childCount() - 1; i >= 0; i--)
        if (item->child(i)->data(0, PlaceholderRole).toBool()) delete item->takeChild(i);
    if (!ok) {
        auto *err = new QTreeWidgetItem(item);
        err->setText(0, httpStatus == 401 ? tr("Anmeldung abgelehnt") : tr("Nicht lesbar"));
        err->setData(0, PlaceholderRole, true);
        err->setFlags(Qt::NoItemFlags);
        m_updating = false;
        emit loadFailed(rel.isEmpty() ? tr("Der Server-Ordner %1 ist nicht lesbar").arg(m_root)
                                      : tr("%1 ist nicht lesbar").arg(absPath(rel)),
                        httpStatus);
        return;
    }
    item->setData(0, LoadedRole, true);
    for (const QString &name : folders)
        addFolder(item, name, rel.isEmpty() ? name : rel + QLatin1Char('/') + name);
    m_updating = false;
}

Qt::CheckState FolderTree::stateOf(const QString &rel) const
{
    bool below = false;
    for (const QString &e : m_excludes) {
        if (rel == e || isBelow(rel, e)) return Qt::Unchecked;
        if (isBelow(e, rel)) below = true;
    }
    return below ? Qt::PartiallyChecked : Qt::Checked;
}

void FolderTree::normalize()
{
    // drop duplicates and paths below an excluded one
    QStringList out;
    for (const QString &e : std::as_const(m_excludes)) {
        if (e.isEmpty()) continue;
        bool covered = false;
        for (const QString &o : std::as_const(m_excludes))
            if (isBelow(e, o)) { covered = true; break; }
        if (!covered && !out.contains(e)) out << e;
    }
    out.sort();
    m_excludes = out;
}

void FolderTree::exclude(const QString &rel)
{
    m_excludes << rel;
    normalize();
}

void FolderTree::include(const QString &rel)
{
    // the folder itself or something below it: just remove those
    QString ancestor;
    for (const QString &e : std::as_const(m_excludes))
        if (isBelow(rel, e)) ancestor = e;
    QStringList kept;
    for (const QString &e : std::as_const(m_excludes))
        if (e != rel && !isBelow(e, rel) && e != ancestor) kept << e;
    m_excludes = kept;
    if (!ancestor.isEmpty()) {
        // an excluded folder above: take it back in, but exclude its other
        // subfolders on the way down (all loaded -- they are on screen)
        QString cur = ancestor;
        while (cur != rel) {
            // rel starts with cur + '/': next = cur plus one more component
            QTreeWidgetItem *node = findItem(cur);
            int slash = rel.indexOf(QLatin1Char('/'), cur.size() + 1);
            QString next = slash < 0 ? rel : rel.left(slash);
            if (node) {
                for (int i = 0; i < node->childCount(); i++) {
                    QString c = node->child(i)->data(0, RelRole).toString();
                    if (!c.isEmpty() && c != next) m_excludes << c;
                }
            }
            cur = next;
        }
    }
    normalize();
}

void FolderTree::onChanged(QTreeWidgetItem *item, int)
{
    if (m_updating || item->data(0, PlaceholderRole).toBool()) return;
    QString rel = item->data(0, RelRole).toString();
    if (rel.isEmpty()) return;
    // the user clicked: decide from the model, not from Qt's new state
    if (stateOf(rel) == Qt::Unchecked) include(rel);
    else exclude(rel);
    refreshStates();
    emit excludesChanged();
}

void FolderTree::refreshStates()
{
    m_updating = true;
    QList<QTreeWidgetItem *> stack;
    if (topLevelItem(0)) stack << topLevelItem(0);
    while (!stack.isEmpty()) {
        QTreeWidgetItem *it = stack.takeLast();
        QString rel = it->data(0, RelRole).toString();
        if (!rel.isEmpty() && !it->data(0, PlaceholderRole).toBool())
            it->setCheckState(0, stateOf(rel));
        for (int i = 0; i < it->childCount(); i++) stack << it->child(i);
    }
    m_updating = false;
}
