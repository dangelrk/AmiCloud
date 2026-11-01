// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
#include "synccontroller.h"
#include "corebridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>

extern "C" {
#include "ui_hooks.h"
}

static const int kMaxLog = 2000;

SyncController::SyncController(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &SyncController::onTimer);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(3000);
    connect(&m_debounce, &QTimer::timeout, this, &SyncController::syncNow);
    connect(&m_watcher, &LocalWatcher::changed, this, &SyncController::onLocalChange);
}

SyncController::~SyncController()
{
    stop();
}

void SyncController::start()
{
    m_settings = AppSettings::load();
    m_password = CredentialStore::password();

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("amicloud-core"));
    m_worker = new CoreWorker;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &CoreWorker::syncStarted, this, &SyncController::onSyncStarted);
    connect(m_worker, &CoreWorker::syncFinished, this, &SyncController::onSyncFinished);
    connect(m_worker, &CoreWorker::accountChecked, this, &SyncController::accountChecked);
    connect(m_worker, &CoreWorker::foldersListed, this, &SyncController::foldersListed);
    connect(CoreBridge::instance(), &CoreBridge::message, this, &SyncController::onMessage);
    connect(CoreBridge::instance(), &CoreBridge::transferProgress, this,
            &SyncController::onTransfer);
    m_thread->start();
    QMetaObject::invokeMethod(m_worker, &CoreWorker::init, Qt::QueuedConnection);

    updateWatch();
    if (!m_settings.isConfigured()) {
        setState(NotConfigured, tr("Nicht eingerichtet"));
    } else if (m_settings.paused) {
        CoreWorker::requestStop();
        setState(Paused, tr("Pausiert"));
    } else {
        setState(Idle, tr("Bereit"));
        QTimer::singleShot(1500, this, &SyncController::syncNow);
    }
}

void SyncController::stop()
{
    if (!m_thread) return;
    m_timer.stop();
    m_debounce.stop();
    m_watcher.watch(QString());
    CoreWorker::requestStop();
    QMetaObject::invokeMethod(m_worker, &CoreWorker::shutdown, Qt::QueuedConnection);
    m_thread->quit();
    // keep the event loop going: the worker may be waiting for a question
    // to be answered in this thread (ui_confirm, BlockingQueuedConnection)
    QElapsedTimer t;
    t.start();
    while (!m_thread->wait(50) && t.elapsed() < 20000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    m_thread = nullptr;
    m_worker = nullptr;
}

Account SyncController::account() const
{
    Account a;
    a.server = AppSettings::normalizeServer(m_settings.server);
    a.user = m_settings.user;
    a.password = m_password;
    a.verifySsl = m_settings.verifySsl;
    return a;
}

int SyncController::checkAccount(const Account &acc)
{
    int id = m_nextReq++;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, acc, id] { w->checkAccount(acc, id); },
                              Qt::QueuedConnection);
    return id;
}

int SyncController::listFolders(const Account &acc, const QString &remotePath)
{
    int id = m_nextReq++;
    QMetaObject::invokeMethod(m_worker,
                              [w = m_worker, acc, remotePath, id] { w->listFolders(acc, remotePath, id); },
                              Qt::QueuedConnection);
    return id;
}

bool SyncController::readyToRun() const
{
    return m_worker && m_settings.isConfigured() && !m_settings.paused;
}

void SyncController::syncNow()
{
    if (!readyToRun()) return;
    if (m_running) { m_again = true; return; }
    m_timer.stop();
    m_debounce.stop();
    m_running = true;
    m_again = false;

    SyncRequest req;
    req.acc = account();
    req.localRoot = m_settings.localFolder;
    req.remoteRoot = AppSettings::normalizeRemote(m_settings.remoteFolder);
    req.excludes = m_settings.excludes;
    req.delMin = m_settings.delMin;
    req.delPct = m_settings.delPct;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, req] { w->runSync(req); },
                              Qt::QueuedConnection);
}

void SyncController::pause()
{
    if (m_settings.paused) return;
    m_settings.paused = true;
    m_settings.save();
    CoreWorker::requestStop();
    m_timer.stop();
    addLog(UI_INFO, tr("Pausiert"));
    if (!m_running) setState(Paused, tr("Pausiert"));
    else { m_statusText = tr("Wird angehalten …"); emit stateChanged(); }
}

void SyncController::resume()
{
    if (!m_settings.paused) return;
    m_settings.paused = false;
    m_settings.save();
    CoreWorker::allowRuns();
    addLog(UI_INFO, tr("Fortgesetzt"));
    if (!m_running) {
        setState(m_settings.isConfigured() ? Idle : NotConfigured, tr("Bereit"));
        syncNow();
    }
}

void SyncController::applySettings(const AppSettings &s, const QString &password,
                                   const QStringList &trashPaths)
{
    m_pendingSettings = s;
    m_pendingPassword = password;
    m_pendingTrash = trashPaths;
    m_havePending = true;
    if (m_running) {
        addLog(UI_INFO, tr("Die Änderungen gelten nach dem laufenden Abgleich"));
        CoreWorker::requestStop();   // the rest follows with the next run
        return;
    }
    applyPending();
    if (readyToRun()) syncNow();
}

void SyncController::applyPending()
{
    if (!m_havePending) return;
    m_havePending = false;
    bool accountChanged = m_pendingSettings.server != m_settings.server ||
                          m_pendingSettings.user != m_settings.user ||
                          !m_pendingPassword.isEmpty();
    bool keepPaused = m_settings.paused;
    m_settings = m_pendingSettings;
    m_settings.paused = keepPaused;
    m_settings.save();
    if (!m_pendingPassword.isEmpty()) {
        if (!CredentialStore::setPassword(m_pendingPassword))
            addLog(UI_ERROR, tr("App-Passwort konnte nicht gespeichert werden: %1")
                                 .arg(CredentialStore::filePath()));
        m_password = m_pendingPassword;
    }
    m_pendingPassword.clear();

    // the new selection is saved before anything is moved: the next run
    // already leaves these folders out
    for (const QString &p : std::as_const(m_pendingTrash)) {
        if (!QFileInfo(p).isDir()) continue;
        if (QFile::moveToTrash(p))
            addLog(UI_INFO, tr("In den Papierkorb gelegt (nicht mehr abgeglichen): %1").arg(p));
        else
            addLog(UI_WARN, tr("Konnte nicht in den Papierkorb gelegt werden, bleibt liegen: %1").arg(p));
    }
    m_pendingTrash.clear();

    if (!m_settings.isConfigured()) setState(NotConfigured, tr("Nicht eingerichtet"));
    else if (m_settings.paused) setState(Paused, tr("Pausiert"));
    else if (m_state == NotConfigured || (m_state == AuthError && accountChanged) ||
             m_state == Error || m_state == Offline)
        setState(Idle, tr("Bereit"));
    if (!m_settings.paused) CoreWorker::allowRuns();
    updateWatch();
}

// the local folder only exists after the first run: (re)watch whenever
// it may have changed
void SyncController::updateWatch()
{
    QString want = m_settings.isConfigured() ? m_settings.localFolder : QString();
    if (want.isEmpty()) { if (m_watcher.active()) m_watcher.watch(QString()); return; }
    if (!m_watcher.active() || m_watchedPath != want) {
        m_watcher.watch(want);
        m_watchedPath = m_watcher.active() ? want : QString();
    }
}

void SyncController::onLocalChange()
{
    if (!readyToRun() || m_state == AuthError) return;
    if (m_running) { m_localDirty = true; return; }   // one more run after this one
    m_debounce.start();
}

void SyncController::onTimer()
{
    syncNow();
}

void SyncController::scheduleNext()
{
    m_timer.stop();
    if (!readyToRun() || m_state == AuthError) return;
    int minutes = m_settings.intervalMin;
    if (m_state == Offline) minutes = qMin(minutes, 1 << qMin(m_offlineRuns, 4));   // 1,2,4,8,16
    m_timer.start(minutes * 60 * 1000);
}

void SyncController::onSyncStarted()
{
    m_progressText.clear();
    setState(Syncing, tr("Gleiche ab …"));
}

static QString summaryText(const SyncResult &r)
{
    QStringList parts;
    if (r.uploaded) parts << QObject::tr("%1 hochgeladen").arg(r.uploaded);
    if (r.downloaded) parts << QObject::tr("%1 heruntergeladen").arg(r.downloaded);
    if (r.deleted) parts << QObject::tr("%1 gelöscht").arg(r.deleted);
    if (r.copies) parts << QObject::tr("%1 Konfliktkopie(n)").arg(r.copies);
    if (r.conflicts) parts << QObject::tr("%1 Konflikt(e)").arg(r.conflicts);
    if (r.kept) parts << QObject::tr("%1 Löschung(en) zurückgehalten").arg(r.kept);
    if (r.skipped) parts << QObject::tr("%1 übersprungen").arg(r.skipped);
    if (r.viruses) parts << QObject::tr("%1 Virus").arg(r.viruses);
    if (r.errors) parts << QObject::tr("%1 Fehler").arg(r.errors);
    return parts.isEmpty() ? QObject::tr("keine Änderungen") : parts.join(QStringLiteral(", "));
}

void SyncController::onSyncFinished(const SyncResult &res)
{
    m_running = false;
    m_progressText.clear();
    emit progressChanged();
    bool hadPending = m_havePending;   // the run was stopped to apply settings

    switch (res.outcome) {
    case SyncResult::Done:
        m_offlineRuns = 0;
        m_lastSync = QDateTime::currentDateTime();
        setState(res.errors ? Error : Idle,
                 tr("Abgeglichen %1 — %2").arg(m_lastSync.toString(QStringLiteral("HH:mm")),
                                               summaryText(res)));
        if (res.copies || res.conflicts)
            emit attention(tr("Konflikt"),
                           tr("Eine Datei wurde auf beiden Seiten geändert. Beide Fassungen "
                              "sind erhalten, die eigene als Konfliktkopie (…_K…)."), false);
        if (res.kept)
            emit attention(tr("Löschungen zurückgehalten"),
                           tr("%1 Löschung(en) wurden nicht ausgeführt. Der nächste Abgleich "
                              "stellt die Dateien wieder her.").arg(res.kept), false);
        break;
    case SyncResult::Aborted:
        if (m_settings.paused) setState(Paused, tr("Pausiert"));
        else if (m_havePending) setState(Idle, tr("Einstellungen werden übernommen"));
        else setState(Error, tr("Abgleich abgebrochen — siehe Aktivität"));
        break;
    case SyncResult::AuthFailed:
        addLog(UI_ERROR, res.text);
        setState(AuthError, tr("Anmeldung abgelehnt — bitte neu anmelden"));
        emit attention(tr("Anmeldung abgelehnt"),
                       tr("Der Server lehnt Benutzer oder App-Passwort ab. Bis zur neuen "
                          "Anmeldung (Einstellungen → Konto) fragt AmiCloud Desktop nicht "
                          "mehr nach, damit der Server das Konto nicht sperrt."), true);
        break;
    case SyncResult::Offline:
        m_offlineRuns++;
        if (m_offlineRuns == 1) addLog(UI_WARN, res.text);
        setState(Offline, tr("Server nicht erreichbar"));
        break;
    case SyncResult::ServerError:
        addLog(UI_ERROR, res.text);
        setState(Error, res.text);
        break;
    }

    applyPending();
    updateWatch();
    if ((m_again || hadPending) && readyToRun() && m_state != AuthError) {
        m_localDirty = false;
        syncNow();
    } else {
        scheduleNext();
        // changes during the run (also our own writes): one follow-up run,
        // which then finds nothing to do and stays quiet
        if (m_localDirty && readyToRun() && m_state != AuthError) m_debounce.start();
        m_localDirty = false;
    }
}

void SyncController::onMessage(int level, const QString &msg)
{
    QString text = msg;
    if (text.startsWith(QStringLiteral("amicloudcore: "))) text.remove(0, 14);
    // the kernel names local files by their full path: show them relative
    // to the sync folder, the line is about the file, not the folder
    if (!m_settings.localFolder.isEmpty()) {
        QString root = QDir::cleanPath(m_settings.localFolder);
        text.replace(root + QLatin1Char('/'), QString());
        text.replace(root, QStringLiteral("."));
    }
    addLog(level, text);
}

void SyncController::onTransfer(qint64 done, qint64 total, const QString &name)
{
    QString shown = QFileInfo(name).fileName();
    if (shown.isEmpty()) shown = name;
    if (total > 0)
        m_progressText = tr("%1 — %2 %").arg(shown).arg(qMin<qint64>(100, done * 100 / total));
    else
        m_progressText = shown;
    emit progressChanged();
}

void SyncController::setState(State s, const QString &text)
{
    m_state = s;
    m_statusText = text;
    emit stateChanged();
}

void SyncController::addLog(int level, const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    LogEntry e{QDateTime::currentDateTime(), level, text};
    m_log.append(e);
    if (m_log.size() > kMaxLog) m_log.remove(0, m_log.size() - kMaxLog);
    emit logAdded(e);
}

void SyncController::clearLog()
{
    m_log.clear();
    emit logCleared();
}
