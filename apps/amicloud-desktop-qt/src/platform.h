// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// platform.h — the little bit that differs per system. macOS: a status
// menu app (LSUIElement) is not brought to the front by showing a
// window, it has to activate itself.
#pragma once

void platformActivate();
