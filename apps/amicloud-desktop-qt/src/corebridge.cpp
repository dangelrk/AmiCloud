// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "corebridge.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QThread>

extern "C" {
#include "ui_hooks.h"
}

CoreBridge *CoreBridge::instance()
{
    static CoreBridge *b = new CoreBridge;
    return b;
}

// Kernel and cmd.c messages are UTF-8 (server names) with ASCII text
static void hook_notify(int level, const char *msg)
{
    CoreBridge::instance()->notify(level, QString::fromUtf8(msg ? msg : ""));
}

static int hook_confirm(const char *msg)
{
    return CoreBridge::instance()->confirm(QString::fromUtf8(msg ? msg : "")) ? 1 : 0;
}

static int hook_progress(long done, long total, const char *name)
{
    return CoreBridge::instance()->progress(done, total, QString::fromUtf8(name ? name : "")) ? 1 : 0;
}

// a sync must never stop on a question it cannot ask: skip == keep both
static UiOverwrite hook_overwrite(int, const char *, long, const char *, const char *,
                                  long, const char *, const char *)
{
    return UI_OW_SKIP;
}

void CoreBridge::install()
{
    UiHooks h;
    h.notify = hook_notify;
    h.confirm = hook_confirm;
    h.progress = hook_progress;
    h.overwrite = hook_overwrite;
    ui_set_hooks(&h);
}

void CoreBridge::notify(int level, const QString &msg)
{
    emit message(level, msg);
}

bool CoreBridge::confirm(const QString &msg)
{
    QObject *target = m_confirmTarget;
    if (!target) return false;
    bool answer = false;
    if (QThread::currentThread() == target->thread()) {
        QMetaObject::invokeMethod(target, "askConfirm", Qt::DirectConnection,
                                  Q_RETURN_ARG(bool, answer), Q_ARG(QString, msg));
    } else {
        QMetaObject::invokeMethod(target, "askConfirm", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, answer), Q_ARG(QString, msg));
    }
    return answer;
}

bool CoreBridge::progress(qint64 done, qint64 total, const QString &name)
{
    if (ui_break_pending()) return false;   // Pause/Quit: stop this transfer too
    if (name.isEmpty()) return true;        // small API request, no display
    // at most ~4 updates a second, but always the first and the last one
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool last = total > 0 && done >= total;
    if (name != m_lastName || last || now - m_lastEmitMs >= 250) {
        m_lastName = name;
        m_lastEmitMs = now;
        emit transferProgress(done, total, name);
    }
    return true;
}
