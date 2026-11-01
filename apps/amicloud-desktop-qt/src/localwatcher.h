// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// localwatcher.h — notices changes in the local sync folder (plan 6.2,
// LocalWatcher). macOS: FSEvents directly (recursive, coalesced by the
// system), not QFileSystemWatcher. Elsewhere nothing yet (phase 7/8);
// the schedule still runs.
//
// Changes to names that are never synced (the kernel's own .amicloudsync*,
// .DS_Store, ...) are ignored -- otherwise every run would trigger the next.
#pragma once

#include <QObject>
#include <QString>

class LocalWatcher : public QObject
{
    Q_OBJECT
public:
    explicit LocalWatcher(QObject *parent = nullptr);
    ~LocalWatcher() override;

    void watch(const QString &path);   // empty = stop
    bool active() const { return m_impl != nullptr; }

signals:
    void changed();

public:
    // called by the platform part (any thread)
    void notify(const QString &path);

private:
    void *m_impl = nullptr;
    QString m_path;
};
