/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#include "searchresultswindow.h"

#include <QTableView>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDateTime>

/* ------------------------------------------------------------------ */
/* SearchResultsModel                                                    */
/* ------------------------------------------------------------------ */

static QString formatSize(long bytes)
{
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024L * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024L * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1) + " GB";
}

SearchResultsModel::SearchResultsModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void SearchResultsModel::setHits(const QVector<SearchHit> &hits)
{
    beginResetModel();
    m_hits = hits;
    endResetModel();
}

const SearchHit *SearchResultsModel::hitAt(int row) const
{
    if (row < 0 || row >= m_hits.size()) return nullptr;
    return &m_hits.at(row);
}

int SearchResultsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_hits.size();
}

int SearchResultsModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant SearchResultsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    const SearchHit *h = hitAt(index.row());
    if (!h) return QVariant();

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColPath: return QString::fromUtf8(h->path) + (h->is_dir ? "/" : "");
        case ColSize: return h->is_dir ? QStringLiteral("-") : formatSize(h->size);
        case ColDate: return h->mtime
                ? QDateTime::fromSecsSinceEpoch(h->mtime).toLocalTime().toString("yyyy-MM-dd hh:mm:ss")
                : QString();
        }
    } else if (role == Qt::TextAlignmentRole && index.column() == ColSize) {
        return int(Qt::AlignRight | Qt::AlignVCenter);
    }
    return QVariant();
}

QVariant SearchResultsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    switch (section) {
    case ColPath: return QObject::tr("Pfad");
    case ColSize: return QObject::tr("Groesse");
    case ColDate: return QObject::tr("Datum");
    }
    return QVariant();
}

/* ------------------------------------------------------------------ */
/* SearchResultsWindow                                                   */
/* ------------------------------------------------------------------ */

SearchResultsWindow::SearchResultsWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Suchergebnisse"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(640, 400);

    m_model = new SearchResultsModel(this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(SearchResultsModel::ColPath, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_statusLabel = new QLabel(this);

    auto *gotoButton = new QPushButton(tr("Gehe zu"), this);
    auto *closeButton = new QPushButton(tr("Schliessen"), this);
    connect(gotoButton, &QPushButton::clicked, this, [this]() {
        activateRow(m_table->currentIndex().row());
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_statusLabel, 1);
    buttonRow->addWidget(gotoButton);
    buttonRow->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttonRow);

    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex &idx) {
        activateRow(idx.row());
    });
    connect(m_table, &QTableView::activated, this, [this](const QModelIndex &idx) {
        activateRow(idx.row());
    });
}

void SearchResultsWindow::setResults(const QVector<SearchHit> &hits, bool truncated)
{
    m_model->setHits(hits);
    QString status = tr("%1 Treffer").arg(hits.size());
    if (truncated) status += tr(" (gekappt, Server-Limit erreicht)");
    m_statusLabel->setText(status);
    if (!hits.isEmpty()) m_table->selectRow(0);
}

void SearchResultsWindow::activateRow(int row)
{
    const SearchHit *h = m_model->hitAt(row);
    if (h) emit activated(*h);
}
