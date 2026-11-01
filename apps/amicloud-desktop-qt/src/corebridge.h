// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// corebridge.h — the UiBridge of plan 6.2: the C core's ui_hooks
// (notify/confirm/progress, all called from the one worker thread) as Qt
// signals. confirm() blocks the worker until the GUI thread has asked the
// user (mass deletion guard, plan 5); progress() returns 0 while a break
// is pending, so Pause also stops the transfer in flight.
#pragma once

#include <QObject>
#include <QString>

class CoreBridge : public QObject
{
    Q_OBJECT
public:
    static CoreBridge *instance();
    // installs the hooks; call once in the GUI thread before the worker starts
    static void install();

    // the object whose slot "bool askConfirm(QString)" answers ui_confirm()
    // (lives in the GUI thread); nullptr = always "no" (the safe choice)
    void setConfirmTarget(QObject *target) { m_confirmTarget = target; }

    // called by the hooks (worker thread)
    void notify(int level, const QString &msg);
    bool confirm(const QString &msg);
    bool progress(qint64 done, qint64 total, const QString &name);

signals:
    void message(int level, const QString &msg);
    void transferProgress(qint64 done, qint64 total, const QString &name);

private:
    CoreBridge() = default;
    QObject *m_confirmTarget = nullptr;
    qint64 m_lastEmitMs = 0;
    QString m_lastName;
};
