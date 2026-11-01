// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// synccontroller.h — schedule, triggers and state of the desktop client
// (plan 6.2). Owns the worker thread; exactly one request runs in the
// core at a time. Settings changed while a run is busy are applied after
// it (never in the middle: a folder taken out of the selection must not
// be moved to the trash while a run with the old selection could still
// see it "deleted locally").
#pragma once

#include "appsettings.h"
#include "coreworker.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QVector>

class QThread;

struct LogEntry
{
    QDateTime time;
    int level;       // UI_INFO, UI_WARN, UI_ERROR (ui_hooks.h)
    QString text;
};

class SyncController : public QObject
{
    Q_OBJECT
public:
    enum State { NotConfigured, Idle, Syncing, Paused, Offline, AuthError, Error };
    Q_ENUM(State)

    explicit SyncController(QObject *parent = nullptr);
    ~SyncController() override;

    void start();                 // after the hooks are installed
    void stop();                  // before quitting: break the run, wait for the thread

    State state() const { return m_state; }
    QString statusText() const { return m_statusText; }
    QString progressText() const { return m_progressText; }
    const AppSettings &settings() const { return m_settings; }
    Account account() const;
    const QVector<LogEntry> &log() const { return m_log; }
    void clearLog();
    QDateTime lastSync() const { return m_lastSync; }

    // queued in the worker; results come back as signals with reqId
    int checkAccount(const Account &acc);
    int listFolders(const Account &acc, const QString &remotePath);

    // New settings (and password, empty = unchanged). trashPaths: local
    // folders (absolute) to move to the trash once no run is busy --
    // folders just taken out of the selection (plan 6.3).
    void applySettings(const AppSettings &s, const QString &password,
                       const QStringList &trashPaths);

public slots:
    void syncNow();
    void pause();
    void resume();

signals:
    void stateChanged();
    void progressChanged();
    void logAdded(const LogEntry &e);
    void logCleared();
    void accountChecked(int reqId, const AccountInfo &info);
    void foldersListed(int reqId, const QString &remotePath, bool ok, int httpStatus,
                       const QStringList &folders);
    // something the user should see even with the menu closed
    void attention(const QString &title, const QString &text, bool error);

private slots:
    void onMessage(int level, const QString &msg);
    void onTransfer(qint64 done, qint64 total, const QString &name);
    void onSyncStarted();
    void onSyncFinished(const SyncResult &res);
    void onTimer();

private:
    void setState(State s, const QString &text);
    void addLog(int level, const QString &text);
    void applyPending();
    void scheduleNext();
    bool readyToRun() const;

    AppSettings m_settings;
    QString m_password;
    State m_state = NotConfigured;
    QString m_statusText, m_progressText;
    QThread *m_thread = nullptr;
    CoreWorker *m_worker = nullptr;
    QTimer m_timer;
    bool m_running = false;       // a sync request is queued or running
    bool m_again = false;         // "sync now" while running: one more run after it
    int m_nextReq = 1;
    QDateTime m_lastSync;
    QVector<LogEntry> m_log;
    int m_offlineRuns = 0;

    bool m_havePending = false;
    AppSettings m_pendingSettings;
    QString m_pendingPassword;
    QStringList m_pendingTrash;
};
