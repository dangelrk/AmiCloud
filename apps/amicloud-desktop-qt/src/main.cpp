// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// AmiCloud Desktop — background folder sync with the AmiCloud server
// (docs/design/desktop-sync-plan.md, section 6). Phase 5: setup wizard,
// selection tree, sync on demand and by interval, status menu, activity,
// pause. FSEvents, autostart and the .dmg follow in phase 6.
#include "appsettings.h"
#include "corebridge.h"
#include "coreworker.h"
#include "platform.h"
#include "synccontroller.h"
#include "trayapp.h"

#include <QApplication>
#include <QLockFile>
#include <QMessageBox>
#include <QSystemTrayIcon>

extern "C" {
#include "amicloud.h"
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("AmiCloud"));
    QApplication::setApplicationName(QStringLiteral("AmiCloud Desktop"));
    QApplication::setApplicationVersion(QStringLiteral(AC_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);   // lives in the menu bar

    // one instance per user: two would sync the same folder at once
    QLockFile lock(AppSettings::configDir() + QStringLiteral("/amicloud-desktop.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100)) {
        platformActivate();
        QMessageBox::information(nullptr, QStringLiteral("AmiCloud Desktop"),
                                 QObject::tr("AmiCloud Desktop läuft bereits (Symbol in der Menüleiste)."));
        return 0;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, QStringLiteral("AmiCloud Desktop"),
                              QObject::tr("Keine Menüleiste bzw. kein Infobereich verfügbar."));
        return 1;
    }

    qRegisterMetaType<Account>();
    qRegisterMetaType<SyncRequest>();
    qRegisterMetaType<SyncResult>();
    qRegisterMetaType<AccountInfo>();

    CoreBridge::install();
    SyncController ctl;
    TrayApp tray(&ctl);
    CoreBridge::instance()->setConfirmTarget(&tray);
    ctl.start();
    tray.show();

    int rc = app.exec();
    CoreBridge::instance()->setConfirmTarget(nullptr);
    ctl.stop();
    return rc;
}
