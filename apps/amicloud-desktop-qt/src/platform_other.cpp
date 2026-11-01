// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "platform.h"

void platformActivate()
{
}

// Windows (Run key) and Linux (~/.config/autostart) follow in phase 7/8
bool platformAutostartSupported() { return false; }
bool platformAutostartEnabled() { return false; }

bool platformSetAutostart(bool, QString *error)
{
    if (error) *error = QStringLiteral("Auf diesem System noch nicht möglich.");
    return false;
}
