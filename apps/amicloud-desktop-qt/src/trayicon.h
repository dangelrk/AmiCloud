// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// trayicon.h — the status menu icon, drawn in code: a cloud with the
// state cut out of it (arrows while syncing, bars when paused, "!" on
// errors, outline only when offline or not set up). On macOS it is a
// template image (mask), so the menu bar colours it for light and dark.
#pragma once

#include "synccontroller.h"

#include <QIcon>

QIcon makeTrayIcon(SyncController::State state);
