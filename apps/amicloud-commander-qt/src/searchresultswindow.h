/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef SEARCHRESULTSWINDOW_H
#define SEARCHRESULTSWINDOW_H

#include <QDialog>
#include <QAbstractTableModel>
#include <QVector>

extern "C" {
#include "search.h" /* SearchHit -- shared cloud/local result shape */
}

class QTableView;

/* Thin QAbstractTableModel over a QVector<SearchHit> (columns Pfad/Groesse/
 * Datum) -- mirrors PanelTableModel's shape but for a flat, full-path
 * result list rather than one directory's worth of names. */
class SearchResultsModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColPath = 0, ColSize, ColDate, ColumnCount };

    explicit SearchResultsModel(QObject *parent = nullptr);

    void setHits(const QVector<SearchHit> &hits);
    const SearchHit *hitAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<SearchHit> m_hits;
};

/* Modeless results window (docs/design/search-plan.md section 5) --
 * a single instance is reused across searches (MainWindow::doSearch()
 * keeps the pointer and calls setResults() again instead of opening a
 * second window). Double-click/Enter/"Gehe zu" all emit activated() with
 * the launching panel expected to jump there (MainWindow::gotoSearchHit()). */
class SearchResultsWindow : public QDialog
{
    Q_OBJECT
public:
    explicit SearchResultsWindow(QWidget *parent = nullptr);

    void setResults(const QVector<SearchHit> &hits, bool truncated);

signals:
    void activated(const SearchHit &hit);

private:
    void activateRow(int row);

    QTableView         *m_table;
    SearchResultsModel  *m_model;
    class QLabel        *m_statusLabel;
};

#endif // SEARCHRESULTSWINDOW_H
