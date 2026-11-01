// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "coreworker.h"

#include <QFile>

#include <atomic>
#include <cstring>
#include <vector>

extern "C" {
#include "amicloud.h"
#include "cmd.h"
#include "http.h"
#include "charset.h"
#include "ui_hooks.h"
#include "progress.h"
#include "sync.h"
#include "amicloud_transport.h"
}

static std::atomic<bool> g_stop{false};

void CoreWorker::requestStop()
{
    g_stop = true;
    ui_break_set();
}

void CoreWorker::allowRuns()
{
    g_stop = false;
}

static void fill_config(AcConfig *cfg, const Account &acc)
{
    std::memset(cfg, 0, sizeof(*cfg));
    QByteArray server = acc.server.toUtf8(), user = acc.user.toUtf8(),
               pw = acc.password.toUtf8();
    while (server.endsWith('/')) server.chop(1);
    std::strncpy(cfg->server, server.constData(), sizeof(cfg->server) - 1);
    std::strncpy(cfg->username, user.constData(), sizeof(cfg->username) - 1);
    std::strncpy(cfg->password, pw.constData(), sizeof(cfg->password) - 1);
    cfg->basepath[0] = '/';
    cfg->verify_ssl = acc.verifySsl ? 1 : 0;
}

void CoreWorker::init()
{
    http_init();
    cs_open(nullptr);   // PC: UTF-8 names, NFC (charset.c)
}

void CoreWorker::shutdown()
{
    http_cleanup();
}

static AccountInfo status_of(const AcConfig *cfg)
{
    AccountInfo info;
    AcStatus st;
    info.ok = cmd_status_ex(cfg, &st) != 0;
    info.httpStatus = cmd_last_status();
    if (!info.ok) {
        if (info.httpStatus == 401 || info.httpStatus == 403)
            info.error = QObject::tr("Anmeldung abgelehnt: Benutzer oder App-Passwort falsch");
        else if (info.httpStatus == 0)
            info.error = QObject::tr("Server nicht erreichbar: %1")
                             .arg(QString::fromUtf8(http_last_error()));
        else
            info.error = QObject::tr("Server antwortet nicht wie erwartet (HTTP %1) — "
                                     "ist die Adresse richtig?").arg(info.httpStatus);
        return info;
    }
    info.version = QString::fromUtf8(st.version);
    info.user = QString::fromUtf8(st.user);
    info.displayName = QString::fromUtf8(st.display_name);
    info.hasQuota = st.has_quota != 0;
    info.usedKb = st.quota_used_kb;
    info.totalKb = st.quota_total_kb;
    return info;
}

void CoreWorker::checkAccount(const Account &acc, int reqId)
{
    AcConfig cfg;
    fill_config(&cfg, acc);
    emit accountChecked(reqId, status_of(&cfg));
}

void CoreWorker::listFolders(const Account &acc, const QString &remotePath, int reqId)
{
    AcConfig cfg;
    AcEntry *entries = nullptr;
    int n = 0;
    QStringList dirs;
    fill_config(&cfg, acc);
    QByteArray path = remotePath.toUtf8();
    bool ok = cmd_list_raw(&cfg, path.constData(), &entries, &n) != 0;
    int status = cmd_last_status();
    if (ok) {
        for (int i = 0; i < n; i++) {
            if (!entries[i].is_dir || sync_name_excluded(entries[i].name)) continue;
            dirs << QString::fromUtf8(entries[i].name);
        }
        cmd_entries_free(entries);
        dirs.sort(Qt::CaseInsensitive);
    }
    emit foldersListed(reqId, remotePath, ok, status, dirs);
}

void CoreWorker::runSync(const SyncRequest &req)
{
    AcConfig cfg;
    SyncResult res;
    fill_config(&cfg, req.acc);

    ui_break_clear();
    if (g_stop) ui_break_set();   // paused between queueing and now
    emit syncStarted();

    // One cheap request first: no sync run against a server that is
    // away or refuses the login. A 401 must not be repeated -- the
    // server's throttle (5 failures) would lock out every client of the
    // account, the Amiga included (plan 6.2).
    AccountInfo info = status_of(&cfg);
    if (!info.ok) {
        res.httpStatus = info.httpStatus;
        res.outcome = (info.httpStatus == 401 || info.httpStatus == 403) ? SyncResult::AuthFailed
                    : info.httpStatus == 0 ? SyncResult::Offline : SyncResult::ServerError;
        res.text = info.error;
        emit syncFinished(res);
        return;
    }

    // the kernel joins "<root>/<rel>": the server root is "" (-> "/rel")
    QByteArray remote = req.remoteRoot == QLatin1String("/") ? QByteArray()
                                                            : req.remoteRoot.toUtf8();
    QByteArray local = QFile::encodeName(req.localRoot);
    while (local.size() > 1 && local.endsWith('/')) local.chop(1);

    std::vector<QByteArray> exBytes;
    std::vector<const char *> exPtrs;
    exBytes.reserve(static_cast<size_t>(req.excludes.size()));
    for (const QString &e : req.excludes) {
        exBytes.push_back(e.toUtf8());
        exPtrs.push_back(exBytes.back().constData());
    }

    SyncTransport *t = amicloud_transport_open(&cfg);
    if (!t) {
        res.outcome = SyncResult::ServerError;
        res.text = tr("Kein Speicher");
        emit syncFinished(res);
        return;
    }

    SyncOptions o;
    sync_options_init(&o);
    o.verbose = 1;                 // one activity line per action
    o.del_min = req.delMin;
    o.del_pct = req.delPct;
    o.force_deletes = req.forceDeletes ? 1 : 0;
    o.excludes = exPtrs.empty() ? nullptr : exPtrs.data();
    o.n_excludes = static_cast<int>(exPtrs.size());

    Progress pr;
    progress_reset(&pr);
    SyncSummary sum;
    cloudsync_run_ex(t, local.constData(), remote.constData(), &o, &pr, &sum);
    amicloud_transport_close(t);

    res.uploaded = sum.uploaded;
    res.downloaded = sum.downloaded;
    res.unchanged = sum.unchanged;
    res.deleted = sum.deleted;
    res.conflicts = sum.conflicts;
    res.errors = sum.errors;
    res.skipped = sum.skipped;
    res.viruses = sum.viruses;
    res.copies = sum.copies;
    res.kept = sum.kept;
    res.aborted = sum.aborted != 0;
    res.httpStatus = cmd_last_status();
    if (res.errors && (res.httpStatus == 401 || res.httpStatus == 403)) {
        res.outcome = SyncResult::AuthFailed;   // e.g. app password revoked mid-run
        res.text = tr("Anmeldung abgelehnt: App-Passwort widerrufen?");
    } else {
        res.outcome = res.aborted ? SyncResult::Aborted : SyncResult::Done;
    }
    ui_break_clear();
    emit syncFinished(res);
}
