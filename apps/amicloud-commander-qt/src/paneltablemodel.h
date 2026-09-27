/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef PANELTABLEMODEL_H
#define PANELTABLEMODEL_H

#include <QAbstractTableModel>

extern "C" {
#include "panel.h"
}

/* Thin QAbstractTableModel over a PanelList (panel.h) — the same entry
 * type serves both a local and a remote listing (GUI-Commander-plan
 * section 5.4). Row 0 is a synthetic ".." entry whenever the panel isn't
 * at its root; PanelTableModel owns that offset so PanelWidget can just
 * index by PanelList position everywhere else. */
class PanelTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColName = 0, ColSize, ColModified, ColumnCount };

    explicit PanelTableModel(QObject *parent = nullptr);

    /* Takes ownership of list's contents (shallow-copies the struct;
     * PanelTableModel calls panel_list_free() on the previous contents
     * and whenever replaced/destroyed). */
    void setList(const PanelList &list, bool showUpEntry);
    const PanelEntry *entryAt(int row) const; /* nullptr for the ".." row */
    bool isUpRow(int row) const { return m_showUp && row == 0; }

    /* Linear name lookup in the already-loaded listing — used by the F5
     * copy-flow rework to look up a destination file's existing size/date/
     * MD5 for the overwrite dialog without an extra network round-trip
     * (nullptr if no non-directory entry with that name is currently
     * listed). */
    const PanelEntry *entryByName(const QString &name) const;

    /* Like entryByName(), but matches a file OR a directory and returns
     * the model row (accounting for the ".." offset) rather than the
     * entry itself -- used by revealEntry() to select a search hit after
     * navigating there, where the hit may well be a directory. -1 if not
     * found in the current listing. */
    int rowForName(const QString &name) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    ~PanelTableModel() override;

private:
    PanelList m_list{};
    bool      m_showUp = false;
};

#endif // PANELTABLEMODEL_H
