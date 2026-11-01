// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// coreworker.h — the ONE thread that calls into the C core (plan 6.2).
// The core has global state (g_curl, the last HTTP status, the hooks,
// the break flag), so login checks, folder listings for the selection
// tree and sync runs all go through this object's thread, one after the
// other. Requests are queued with QMetaObject::invokeMethod(); results
// come back as signals (queued to the GUI thread).
#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

struct Account
{
    QString server, user, password;
    bool verifySsl = true;
};

struct SyncRequest
{
    Account acc;
    QString localRoot;
    QString remoteRoot;      // "/" or "/Fotos"
    QStringList excludes;
    int delMin = 20, delPct = 50;
    bool forceDeletes = false;
};

struct SyncResult
{
    enum Outcome { Done, AuthFailed, Offline, ServerError, Aborted };
    Outcome outcome = Done;
    int httpStatus = 0;
    int uploaded = 0, downloaded = 0, unchanged = 0, deleted = 0, conflicts = 0,
        errors = 0, skipped = 0, viruses = 0, copies = 0, kept = 0;
    bool aborted = false;    // the kernel stopped early (scan error, break)
    QString text;            // one line for the status menu
};

struct AccountInfo
{
    bool ok = false;
    int httpStatus = 0;      // 401 = wrong user or password, 0 = no answer
    QString error;
    QString version, user, displayName;
    bool hasQuota = false;
    qint64 usedKb = 0, totalKb = 0;
};

class CoreWorker : public QObject
{
    Q_OBJECT
public:
    explicit CoreWorker(QObject *parent = nullptr) : QObject(parent) {}

    // Pause/Quit from the GUI thread: the running sync stops between two
    // actions and the transfer in flight is cancelled (ui_break_set());
    // stays set for runs that start later until allowRuns().
    static void requestStop();
    static void allowRuns();

public slots:
    void init();          // http_init(), in the worker thread
    void shutdown();      // http_cleanup()

    void checkAccount(const Account &acc, int reqId);
    // direct subfolders of remotePath (absolute server path, "/" = root)
    void listFolders(const Account &acc, const QString &remotePath, int reqId);
    void runSync(const SyncRequest &req);

signals:
    void accountChecked(int reqId, const AccountInfo &info);
    void foldersListed(int reqId, const QString &remotePath, bool ok, int httpStatus,
                       const QStringList &folders);
    void syncStarted();
    void syncFinished(const SyncResult &res);
};

Q_DECLARE_METATYPE(Account)
Q_DECLARE_METATYPE(SyncRequest)
Q_DECLARE_METATYPE(SyncResult)
Q_DECLARE_METATYPE(AccountInfo)
