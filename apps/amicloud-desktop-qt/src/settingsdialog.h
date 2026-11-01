// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// settingsdialog.h — account, folders (local folder, start folder,
// selection tree) and sync options. A folder taken out of the selection
// that exists locally is asked about: to the trash, or keep it (then it
// is no longer synced) -- the kernel never deletes it itself (plan 6.3).
#pragma once

#include "appsettings.h"
#include "coreworker.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTabWidget;
class FolderTree;
class SyncController;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(SyncController *ctl, QWidget *parent = nullptr);

    void showAccountTab();

protected:
    void accept() override;

private:
    QWidget *buildAccountTab();
    QWidget *buildFolderTab();
    QWidget *buildSyncTab();
    Account formAccount() const;
    void loadTree();

    SyncController *m_ctl;
    AppSettings m_orig;
    QTabWidget *m_tabs;
    QLineEdit *m_server, *m_user, *m_password;
    QCheckBox *m_insecure;
    QLabel *m_accountStatus;
    int m_checkReq = 0;
    QLineEdit *m_local, *m_remote;
    FolderTree *m_tree;
    QLabel *m_treeError;
    bool m_treeLoaded = false;
    QSpinBox *m_interval, *m_delMin, *m_delPct;
    QCheckBox *m_autostart = nullptr;
};
