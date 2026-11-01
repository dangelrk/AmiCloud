// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// trayapp.h — the status menu (plan 6.2): state, sync now, pause, open
// folder, activity, settings, quit. Also answers the core's questions
// (ui_confirm: mass deletion guard) in the GUI thread.
#pragma once

#include <QObject>
#include <QPointer>

class QAction;
class QMenu;
class QSystemTrayIcon;
class ActivityWindow;
class SettingsDialog;
class SetupWizard;
class SyncController;

class TrayApp : public QObject
{
    Q_OBJECT
public:
    explicit TrayApp(SyncController *ctl, QObject *parent = nullptr);
    void show();

public slots:
    // called by CoreBridge from the worker (BlockingQueuedConnection)
    bool askConfirm(const QString &msg);

    void openSetup();
    void openSettings();
    void openActivity();

private slots:
    void updateState();
    void updateProgress();
    void onAttention(const QString &title, const QString &text, bool error);

private:
    SyncController *m_ctl;
    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    QAction *m_status, *m_progress, *m_syncNow, *m_pause, *m_openFolder, *m_openWeb, *m_setup;
    QPointer<ActivityWindow> m_activity;
    QPointer<SettingsDialog> m_settings;
    QPointer<SetupWizard> m_wizard;
    bool m_quitting = false;
};
