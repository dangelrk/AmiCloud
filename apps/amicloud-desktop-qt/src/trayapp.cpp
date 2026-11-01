// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "trayapp.h"
#include "activitywindow.h"
#include "platform.h"
#include "settingsdialog.h"
#include "setupwizard.h"
#include "synccontroller.h"
#include "trayicon.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QUrl>

extern "C" {
#include "amicloud.h"
}

TrayApp::TrayApp(SyncController *ctl, QObject *parent) : QObject(parent), m_ctl(ctl)
{
    m_tray = new QSystemTrayIcon(this);
    m_menu = new QMenu;
    m_status = m_menu->addAction(QString());
    m_status->setEnabled(false);
    m_progress = m_menu->addAction(QString());
    m_progress->setEnabled(false);
    m_progress->setVisible(false);
    m_menu->addSeparator();
    m_syncNow = m_menu->addAction(tr("Jetzt abgleichen"), m_ctl, &SyncController::syncNow);
    m_pause = m_menu->addAction(tr("Pause"), this, [this] {
        if (m_ctl->settings().paused) m_ctl->resume();
        else m_ctl->pause();
    });
    m_menu->addSeparator();
    m_openFolder = m_menu->addAction(tr("Ordner öffnen"), this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctl->settings().localFolder));
    });
    m_openWeb = m_menu->addAction(tr("Im Browser öffnen"), this, [this] {
        QDesktopServices::openUrl(QUrl(AppSettings::normalizeServer(m_ctl->settings().server) +
                                       QStringLiteral("/web.php")));
    });
    m_menu->addSeparator();
    m_menu->addAction(tr("Aktivität …"), this, &TrayApp::openActivity);
    m_menu->addAction(tr("Einstellungen …"), this, &TrayApp::openSettings);
    m_setup = m_menu->addAction(tr("Einrichten …"), this, &TrayApp::openSetup);
    m_menu->addSeparator();
    m_menu->addAction(tr("Über AmiCloud Desktop"), this, [] {
        platformActivate();
        QMessageBox::about(nullptr, tr("Über AmiCloud Desktop"),
                           tr("<b>AmiCloud Desktop %1</b><br>Ordnerabgleich mit dem "
                              "AmiCloud-Server.<br><br>© 2026 Ronny Krueger, GPL-3.0-or-later")
                               .arg(QStringLiteral(AC_VERSION)));
    });
    m_menu->addAction(tr("Beenden"), qApp, [this] {
        m_quitting = true;
        qApp->quit();
    });
    m_tray->setContextMenu(m_menu);
    m_tray->setToolTip(tr("AmiCloud Desktop"));

    connect(m_menu, &QMenu::aboutToShow, this, [this] { updateState(); updateProgress(); });
    connect(m_ctl, &SyncController::stateChanged, this, &TrayApp::updateState);
    connect(m_ctl, &SyncController::progressChanged, this, &TrayApp::updateProgress);
    connect(m_ctl, &SyncController::attention, this, &TrayApp::onAttention);
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &TrayApp::openActivity);
    updateState();
}

void TrayApp::show()
{
    m_tray->show();
    if (m_ctl->state() == SyncController::NotConfigured)
        QTimer::singleShot(300, this, &TrayApp::openSetup);
}

void TrayApp::updateState()
{
    SyncController::State st = m_ctl->state();
    m_tray->setIcon(makeTrayIcon(st));
    m_status->setText(m_ctl->statusText());
    m_tray->setToolTip(tr("AmiCloud Desktop — %1").arg(m_ctl->statusText()));
    bool configured = st != SyncController::NotConfigured;
    bool paused = m_ctl->settings().paused;
    m_syncNow->setEnabled(configured && !paused && st != SyncController::Syncing);
    m_pause->setEnabled(configured);
    m_pause->setText(paused ? tr("Fortsetzen") : tr("Pause"));
    m_openFolder->setEnabled(!m_ctl->settings().localFolder.isEmpty());
    m_openWeb->setEnabled(!m_ctl->settings().server.isEmpty());
}

void TrayApp::updateProgress()
{
    QString p = m_ctl->progressText();
    m_progress->setText(p);
    m_progress->setVisible(!p.isEmpty() && m_ctl->state() == SyncController::Syncing);
}

void TrayApp::onAttention(const QString &title, const QString &text, bool error)
{
    m_tray->showMessage(title, text, error ? QSystemTrayIcon::Critical : QSystemTrayIcon::Information,
                        10000);
    if (error && m_ctl->state() == SyncController::AuthError) {
        openSettings();
        if (m_settings) m_settings->showAccountTab();
    }
}

bool TrayApp::askConfirm(const QString &msg)
{
    if (m_quitting) return false;
    m_tray->showMessage(tr("Rückfrage"), tr("AmiCloud Desktop wartet auf eine Antwort."),
                        QSystemTrayIcon::Warning, 10000);
    platformActivate();
    QMessageBox box;
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("AmiCloud Desktop"));
    box.setText(msg);
    QPushButton *del = box.addButton(tr("Löschen"), QMessageBox::DestructiveRole);
    QPushButton *keep = box.addButton(tr("Nicht löschen"), QMessageBox::RejectRole);
    box.setDefaultButton(keep);
    box.setEscapeButton(keep);
    box.exec();
    return box.clickedButton() == del && !m_quitting;
}

void TrayApp::openSetup()
{
    if (!m_wizard) {
        m_wizard = new SetupWizard(m_ctl);
        m_wizard->setAttribute(Qt::WA_DeleteOnClose);
    }
    platformActivate();
    m_wizard->show();
    m_wizard->raise();
    m_wizard->activateWindow();
}

void TrayApp::openSettings()
{
    if (m_ctl->state() == SyncController::NotConfigured) { openSetup(); return; }
    if (!m_settings) {
        m_settings = new SettingsDialog(m_ctl);
        m_settings->setAttribute(Qt::WA_DeleteOnClose);
    }
    platformActivate();
    m_settings->show();
    m_settings->raise();
    m_settings->activateWindow();
}

void TrayApp::openActivity()
{
    if (!m_activity) {
        m_activity = new ActivityWindow(m_ctl);
        m_activity->setAttribute(Qt::WA_DeleteOnClose);
    }
    platformActivate();
    m_activity->show();
    m_activity->raise();
    m_activity->activateWindow();
}
