// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// No file watcher outside macOS yet (Windows: ReadDirectoryChangesW,
// Linux: inotify -- phase 7/8). The schedule keeps syncing.
#include "localwatcher.h"

LocalWatcher::LocalWatcher(QObject *parent) : QObject(parent) {}
LocalWatcher::~LocalWatcher() = default;

void LocalWatcher::watch(const QString &path)
{
    m_path = path;
}

void LocalWatcher::notify(const QString &)
{
    emit changed();
}
