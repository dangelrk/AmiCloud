// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "platform.h"

#import <AppKit/AppKit.h>

void platformActivate()
{
    [NSApp activate];   // macOS 14+, the app targets 15
}
