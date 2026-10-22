/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "transferjob.h"

extern "C" {
#include "amicloud.h"
}

class PanelWidget;
class QProgressDialog;
class QLabel;
class QAction;
class SearchResultsWindow;
struct SearchHit;

/* Top-level Commander window (GUI-Commander-plan section 3/6). Owns the
 * one shared AcConfig (single cloud account per session, matching the
 * shell) and the two freely Local<->Cloud switchable panels. */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const AcConfig &cfg, QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onPanelActivated(PanelWidget *which);
    /* Connected to both panels' PanelWidget::busy(bool) signal — nested
     * (counted) so two overlapping Cloud round-trips don't have the first
     * one's "done" prematurely clear the indicator the second one still
     * needs. See PanelWidget::reload(). */
    void setBusy(bool on);
    void updateQuota(bool force);

private:
    void doReload();     /* F2 */
    void doInfo();       /* F3 */
    void doCopy();       /* F5 */
    void doRenameMove(); /* F6 */
    void doMkdir();      /* F7 */
    void doDelete();     /* F8 / Del */
    void doChecksum();   /* Extras-Menue: Pruefsumme anzeigen */
    void doSearch();     /* F4 */
    void doAbout();      /* Hilfe-Menue: Ueber... */

    void gotoSearchHit(const SearchHit &hit);

    void startJob(TransferJob::Op op, QVector<TransferJob::Item> items);
    PanelWidget *activePanel() const { return m_active; }
    PanelWidget *otherPanel() const { return m_active == m_left ? m_right : m_left; }

    AcConfig         m_cfg;
    PanelWidget     *m_left;
    PanelWidget     *m_right;
    PanelWidget     *m_active;
    TransferJob     *m_job = nullptr;
    QProgressDialog *m_progressDlg = nullptr;
    QLabel          *m_statusLabel;
    QLabel          *m_busyLabel;
    QLabel          *m_quotaLabel;
    qint64           m_quotaTime = 0;  /* ms since epoch of the last status request */
    int              m_busyCount = 0;
    QAction         *m_hideHiddenAction = nullptr;

    /* Search (docs/design/search-plan.md section 5): single reused
     * result-window instance, and the panel that was active when the
     * search was launched -- gotoSearchHit() jumps that panel, not
     * whichever one happens to be active when the user picks a hit. */
    SearchResultsWindow *m_searchResults = nullptr;
    PanelWidget         *m_searchLaunchPanel = nullptr;
};

#endif // MAINWINDOW_H
