// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// platform.h — the little bit that differs per system. macOS: a status
// menu app (LSUIElement) is not brought to the front by showing a
// window, it has to activate itself; start at login via SMAppService.
#pragma once

#include <QString>

void platformActivate();

// start at login (macOS: SMAppService.mainApp; elsewhere not yet)
bool platformAutostartSupported();
bool platformAutostartEnabled();
// false with a message in *error
bool platformSetAutostart(bool on, QString *error);
