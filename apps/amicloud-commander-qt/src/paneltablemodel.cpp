/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#include "paneltablemodel.h"

#include <QVariant>
#include <QFont>
#include <QColor>

static QString formatSize(long bytes)
{
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024L * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024L * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1) + " GB";
}

PanelTableModel::PanelTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

PanelTableModel::~PanelTableModel()
{
    panel_list_free(&m_list);
}

void PanelTableModel::setList(const PanelList &list, bool showUpEntry)
{
    beginResetModel();
    panel_list_free(&m_list);
    m_list    = list;
    m_showUp  = showUpEntry;
    endResetModel();
}

const PanelEntry *PanelTableModel::entryAt(int row) const
{
    int idx = m_showUp ? row - 1 : row;
    if (idx < 0 || idx >= m_list.n) return nullptr;
    return &m_list.items[idx];
}

const PanelEntry *PanelTableModel::entryByName(const QString &name) const
{
    QByteArray utf8 = name.toUtf8();
    for (int i = 0; i < m_list.n; i++) {
        if (!m_list.items[i].is_dir && utf8 == m_list.items[i].name) return &m_list.items[i];
    }
    return nullptr;
}

int PanelTableModel::rowForName(const QString &name) const
{
    QByteArray utf8 = name.toUtf8();
    for (int i = 0; i < m_list.n; i++) {
        if (utf8 == m_list.items[i].name) return i + (m_showUp ? 1 : 0);
    }
    return -1;
}

int PanelTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_list.n + (m_showUp ? 1 : 0);
}

int PanelTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant PanelTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();

    if (m_showUp && index.row() == 0) {
        if (role == Qt::DisplayRole && index.column() == ColName) return QStringLiteral("..");
        if (role == Qt::FontRole && index.column() == ColName) { QFont f; f.setBold(true); return f; }
        return QVariant();
    }

    const PanelEntry *e = entryAt(index.row());
    if (!e) return QVariant();

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName:     return QString::fromUtf8(e->name) + (e->is_dir ? "/" : "");
        case ColSize:     return e->is_dir ? QStringLiteral("-") : formatSize(e->size);
        case ColModified: return QString::fromUtf8(e->modified);
        }
    } else if (role == Qt::FontRole && index.column() == ColName && e->is_dir) {
        QFont f; f.setBold(true); return f;
    } else if (role == Qt::BackgroundRole && e->marked) {
        return QVariant(QColor(210, 230, 255));
    } else if (role == Qt::TextAlignmentRole && index.column() == ColSize) {
        return int(Qt::AlignRight | Qt::AlignVCenter);
    }
    return QVariant();
}

QVariant PanelTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    switch (section) {
    case ColName:     return QObject::tr("Name");
    case ColSize:     return QObject::tr("Groesse");
    case ColModified: return QObject::tr("Datum");
    }
    return QVariant();
}
