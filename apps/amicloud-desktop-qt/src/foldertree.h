// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// foldertree.h — the server folder tree with check boxes (plan 6.3).
// The truth is the exclusion list, not the check boxes: a folder is
// unchecked if it or a folder above it is excluded, partly checked if
// something below it is excluded, else checked. Folders are loaded when
// opened (through the worker thread, so a listing waits while a sync
// is busy). New server folders are therefore included automatically.
#pragma once

#include "coreworker.h"

#include <QHash>
#include <QStringList>
#include <QTreeWidget>

class SyncController;

class FolderTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit FolderTree(SyncController *ctl, QWidget *parent = nullptr);

    // (re)loads the tree below remoteRoot ("/" or "/Fotos")
    void load(const Account &acc, const QString &remoteRoot, const QStringList &excludes);
    QStringList excludes() const { return m_excludes; }

signals:
    void excludesChanged();
    void loadFailed(const QString &message, int httpStatus);

private slots:
    void onExpanded(QTreeWidgetItem *item);
    void onChanged(QTreeWidgetItem *item, int column);
    void onListed(int reqId, const QString &remotePath, bool ok, int httpStatus,
                  const QStringList &folders);

private:
    QTreeWidgetItem *addFolder(QTreeWidgetItem *parent, const QString &name, const QString &rel);
    QTreeWidgetItem *findItem(const QString &rel) const;
    void request(QTreeWidgetItem *item);
    QString absPath(const QString &rel) const;
    Qt::CheckState stateOf(const QString &rel) const;
    void include(const QString &rel);
    void exclude(const QString &rel);
    void refreshStates();
    void normalize();

    SyncController *m_ctl;
    Account m_acc;
    QString m_root;
    QStringList m_excludes;
    QHash<int, QString> m_pending;   // reqId -> relative path
    bool m_updating = false;
};
