/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef PANELWIDGET_H
#define PANELWIDGET_H

#include <QWidget>
#include <QVector>

extern "C" {
#include "panel.h"
}

class QComboBox;
class QLineEdit;
class QPushButton;
class QTableView;
class PanelTableModel;

/* One side of the two-pane Commander — either a local or a cloud
 * directory listing, freely switchable (GUI-Commander-plan section 3.1).
 * Both sources are exposed through the exact same PanelEntry/PanelList
 * shape from panel.h, so this widget never needs to know which one it's
 * showing beyond the combo box state and which panel_read() source to
 * pass. */
class PanelWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PanelWidget(QWidget *parent = nullptr);

    void setConfig(const AcConfig &cfg);
    const AcConfig &config() const { return m_cfg; }

    PanelSource source() const { return m_source; }
    QString path() const { return m_path; }

    /* Effective per-source starting point: local_cwd on launch, or
     * cfg.basepath for the cloud side (mirrors the shell's banner). */
    void initLocal(const QString &startPath);
    void initCloud(const QString &startPath);

    void reload();
    void navigateTo(const QString &path);
    void enterSelected();  /* Enter: descend into a directory */
    void goUp();           /* Backspace */

    /* Rows the next operation should act on: current selection if
     * non-empty, otherwise just the focused row (MC convention: acting
     * on "the file under the cursor" when nothing is explicitly marked). */
    QVector<PanelEntry> targetEntries() const;

    /* Full path (native separator for LOCAL, canonical '/' for AMICLOUD) to
     * a child of the panel's current directory. */
    QString childPath(const PanelEntry &e) const;

    /* Looks up name in the already-loaded listing (no network round-trip)
     * — used by the F5 copy-flow overwrite dialog to show the destination
     * file's existing size/date/MD5 alongside the incoming one. nullptr if
     * no such (non-directory) entry is currently listed. */
    const PanelEntry *findEntry(const QString &name) const;

    void setActive(bool active);
    bool isActive() const { return m_active; }

    void setStatusText(const QString &text);

    /* Local-only convenience: filters dotfile entries (name starting with
     * '.') out of the listing — Linux/Mac hidden-file convention, no
     * AmigaDOS equivalent, so this stays a Qt-side, PANEL_LOCAL-only
     * concern rather than something added to the shared panel.c core.
     * Off by default (unfiltered, today's behaviour); MainWindow's
     * "Versteckte Dateien ausblenden" toggle drives it for both panels. */
    void setHideHidden(bool hide);
    bool hideHidden() const { return m_hideHidden; }

    /* Go-to for a search hit (docs/design/search-plan.md section 5):
     * navigates to dir (synchronous reload, like navigateTo()) then
     * selects+scrolls to the row named name -- file or directory alike. */
    void revealEntry(const QString &dir, const QString &name);

signals:
    void activated(PanelWidget *self);
    void requestReloadOther();  /* after a copy/move that changed the other side */
    /* Emitted around any Cloud round-trip (currently just reload()) so
     * MainWindow can show a busy cursor/indicator — see MainWindow::setBusy().
     * true = starting, false = finished. Always paired. */
    void busy(bool on);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

private slots:
    void onSourceChanged(int idx);
    void onPathEditReturn();
    void onDoubleClicked(const QModelIndex &index);
    void onSelectionChanged();

private:
    void applyPathAndReload(const QString &path);

    PanelSource      m_source = PANEL_LOCAL;
    AcConfig         m_cfg{};
    QString          m_path;
    QString          m_localPath;  /* remembered across Local<->Cloud switches */
    QString          m_cloudPath;
    bool             m_active = false;
    bool             m_hideHidden = false;

    QComboBox       *m_sourceCombo;
    QLineEdit       *m_pathEdit;
    QPushButton     *m_upButton;
    QTableView      *m_table;
    PanelTableModel *m_model;
    class QLabel    *m_statusLabel;
};

#endif // PANELWIDGET_H
