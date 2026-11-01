// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "platform.h"

#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>

void platformActivate()
{
    [NSApp activate];   // macOS 14+, the app targets 15
}

bool platformAutostartSupported()
{
    return true;
}

bool platformAutostartEnabled()
{
    SMAppServiceStatus st = SMAppService.mainAppService.status;
    // "requires approval": registered, the user still has to allow it in
    // System Settings -> General -> Login Items -- counts as on
    return st == SMAppServiceStatusEnabled || st == SMAppServiceStatusRequiresApproval;
}

bool platformSetAutostart(bool on, QString *error)
{
    NSError *err = nil;
    BOOL ok = on ? [SMAppService.mainAppService registerAndReturnError:&err]
                 : [SMAppService.mainAppService unregisterAndReturnError:&err];
    if (!ok && error)
        *error = QString::fromNSString(err.localizedDescription ?: @"?");
    if (ok && on && SMAppService.mainAppService.status == SMAppServiceStatusRequiresApproval) {
        if (error)
            *error = QStringLiteral("Bitte in den Systemeinstellungen unter „Allgemein → "
                                    "Anmeldeobjekte“ erlauben.");
        [SMAppService openSystemSettingsLoginItems];
    }
    return ok;
}
