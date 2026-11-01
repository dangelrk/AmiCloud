// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// appsettings.h — the desktop client's configuration (plan 6.2): one
// account, one local folder mirroring the server from a start folder,
// the selection as a list of excluded server paths (new server folders
// are therefore included automatically, plan 6.3), interval and the
// mass deletion thresholds. Stored with QSettings (INI file in the
// per-user config directory).
//
// The app password is NOT in there: CredentialStore keeps it in a file
// of its own with permissions 0600 (decided 31.10.2026, no keychain for
// now) and can later move to the keychain without touching the rest.
#pragma once

#include <QString>
#include <QStringList>

class CredentialStore
{
public:
    static QString password();
    // 0600 from the first byte on (QSaveFile, permissions set before the
    // commit); false if the file could not be written
    static bool setPassword(const QString &pw);
    static void clear();
    static QString filePath();
};

struct AppSettings
{
    QString server;          // base URL, e.g. https://amicloud.example.org
    QString user;
    bool    verifySsl = true;
    QString localFolder;     // absolute local path
    QString remoteFolder = QStringLiteral("/");   // start folder on the server
    QStringList excludes;    // relative to remoteFolder, '/'-joined, no leading '/'
    int     intervalMin = 5;
    int     delMin = 20;     // mass deletion guard (plan 5): 20 files ...
    int     delPct = 50;     // ... or 50 % of the baseline
    bool    paused = false;

    static AppSettings load();
    void save() const;

    // server + user + local folder are set and a password is stored
    bool isConfigured() const;
    // where the app password is created (web UI, plan 6.2)
    QString appPasswordUrl() const;

    static QString configDir();
    static QString normalizeServer(const QString &url);
    static QString normalizeRemote(const QString &path);
};
