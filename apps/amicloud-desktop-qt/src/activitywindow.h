// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// activitywindow.h — what the client did: actions, conflicts, skipped
// names and errors (plan 6.2), newest at the bottom, with a filter.
#pragma once

#include "synccontroller.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QTreeWidget;

class ActivityWindow : public QWidget
{
    Q_OBJECT
public:
    explicit ActivityWindow(SyncController *ctl, QWidget *parent = nullptr);

private:
    void add(const LogEntry &e);
    void rebuild();
    void updateStatus();

    SyncController *m_ctl;
    QLabel *m_status;
    QComboBox *m_filter;
    QTreeWidget *m_list;
};
