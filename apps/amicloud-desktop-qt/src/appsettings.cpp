// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "appsettings.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <memory>

QString AppSettings::configDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                               QFileDevice::ExeOwner);
    return dir;
}

static QSettings *openSettings()
{
    return new QSettings(AppSettings::configDir() + QStringLiteral("/amicloud-desktop.ini"),
                         QSettings::IniFormat);
}

QString AppSettings::normalizeServer(const QString &url)
{
    QString s = url.trimmed();
    while (s.endsWith(QLatin1Char('/'))) s.chop(1);
    if (s.endsWith(QStringLiteral("/api.php"))) s.chop(8);   // pasted the API URL
    if (!s.isEmpty() && !s.contains(QStringLiteral("://"))) s.prepend(QStringLiteral("https://"));
    return s;
}

QString AppSettings::normalizeRemote(const QString &path)
{
    QString s = path.trimmed();
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (s.contains(QStringLiteral("//"))) s.replace(QStringLiteral("//"), QStringLiteral("/"));
    if (!s.startsWith(QLatin1Char('/'))) s.prepend(QLatin1Char('/'));
    while (s.size() > 1 && s.endsWith(QLatin1Char('/'))) s.chop(1);
    return s;
}

AppSettings AppSettings::load()
{
    AppSettings a;
    std::unique_ptr<QSettings> s(openSettings());
    a.server       = s->value("account/server").toString();
    a.user         = s->value("account/user").toString();
    a.verifySsl    = s->value("account/verify_ssl", true).toBool();
    a.localFolder  = s->value("folder/local").toString();
    a.remoteFolder = normalizeRemote(s->value("folder/remote", "/").toString());
    a.excludes     = s->value("folder/excludes").toStringList();
    a.intervalMin  = qBound(1, s->value("sync/interval_min", 5).toInt(), 1440);
    a.delMin       = qMax(1, s->value("sync/del_min", 20).toInt());
    a.delPct       = qBound(1, s->value("sync/del_pct", 50).toInt(), 100);
    a.paused       = s->value("sync/paused", false).toBool();
    return a;
}

void AppSettings::save() const
{
    std::unique_ptr<QSettings> s(openSettings());
    s->setValue("account/server", server);
    s->setValue("account/user", user);
    s->setValue("account/verify_ssl", verifySsl);
    s->setValue("folder/local", localFolder);
    s->setValue("folder/remote", normalizeRemote(remoteFolder));
    s->setValue("folder/excludes", excludes);
    s->setValue("sync/interval_min", intervalMin);
    s->setValue("sync/del_min", delMin);
    s->setValue("sync/del_pct", delPct);
    s->setValue("sync/paused", paused);
    s->sync();
}

bool AppSettings::isConfigured() const
{
    return !server.isEmpty() && !user.isEmpty() && !localFolder.isEmpty() &&
           !CredentialStore::password().isEmpty();
}

QString AppSettings::appPasswordUrl() const
{
    return normalizeServer(server) + QStringLiteral("/web.php?view=settings");
}

// ---------------------------------------------------------------------------

QString CredentialStore::filePath()
{
    return AppSettings::configDir() + QStringLiteral("/credentials");
}

QString CredentialStore::password()
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QString pw = QString::fromUtf8(f.readAll());
    while (pw.endsWith(QLatin1Char('\n')) || pw.endsWith(QLatin1Char('\r'))) pw.chop(1);
    return pw;
}

bool CredentialStore::setPassword(const QString &pw)
{
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(pw.toUtf8());
    f.write("\n");
    if (!f.commit()) return false;
    // belt and braces: an older file may have had other permissions
    return QFile::setPermissions(filePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void CredentialStore::clear()
{
    QFile::remove(filePath());
}
