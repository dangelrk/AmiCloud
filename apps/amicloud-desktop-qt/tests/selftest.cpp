// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ronny Krueger
//
// selftest.cpp — headless integration test of AmiCloud Desktop's non-visual
// parts (SyncController, CoreWorker, CoreBridge, FolderTree logic) against
// a REAL AmiCloud server. Runs with QT_QPA_PLATFORM=offscreen; settings
// and credentials go to Qt's test locations (QStandardPaths test mode).
//
//   AC_TEST_SERVER=http://127.0.0.1:8099 AC_TEST_USER=tester AC_TEST_PASS=... \
//   amicloud-desktop-selftest <empty-work-dir>
//
// The server account should be empty: the test creates /dt_<pid>/... and
// deletes nothing outside it.
#include "appsettings.h"
#include "corebridge.h"
#include "coreworker.h"
#include "foldertree.h"
#include "synccontroller.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidgetItem>

#include <cstdio>
#include <functional>

extern "C" {
#include "ui_hooks.h"
}

static int g_fail = 0, g_ok = 0;
#define CHECK(c) do { if (c) g_ok++; else { g_fail++; std::printf("    FEHLER %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static bool waitFor(const std::function<bool()> &cond, int ms = 30000)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

static void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(data);
    else std::printf("    kann %s nicht schreiben\n", qPrintable(path));
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<fehlt>");
}

// answers ui_confirm() like TrayApp::askConfirm, with a preset answer
class Answerer : public QObject
{
    Q_OBJECT
public:
    bool answer = false;
    int asked = 0;
public slots:
    bool askConfirm(const QString &) { asked++; return answer; }
};

// runs trigger() and waits until one sync run has started and ended
static bool waitRun(SyncController *ctl, const std::function<void()> &trigger, int ms = 60000)
{
    bool saw = false, done = false;
    auto c = QObject::connect(ctl, &SyncController::stateChanged, ctl, [&] {
        if (ctl->state() == SyncController::Syncing) saw = true;
        else if (saw) done = true;
    });
    trigger();
    bool ok = waitFor([&] { return done; }, ms);
    QObject::disconnect(c);
    return ok;
}

static AppSettings base(const QString &server, const QString &user, const QString &local,
                        const QString &remote)
{
    AppSettings s;
    s.server = server;
    s.user = user;
    s.localFolder = local;
    s.remoteFolder = remote;
    s.intervalMin = 60;   // no timer runs during the test
    return s;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("AmiCloud"));
    QApplication::setApplicationName(QStringLiteral("AmiCloud Desktop Selftest"));
    QStandardPaths::setTestModeEnabled(true);

    const QString server = qEnvironmentVariable("AC_TEST_SERVER");
    const QString user = qEnvironmentVariable("AC_TEST_USER");
    const QString pass = qEnvironmentVariable("AC_TEST_PASS");
    if (server.isEmpty() || user.isEmpty() || pass.isEmpty() || argc < 2) {
        std::fprintf(stderr, "AC_TEST_SERVER/AC_TEST_USER/AC_TEST_PASS setzen, "
                             "Aufruf: %s <leeres-arbeitsverzeichnis>\n", argv[0]);
        return 2;
    }
    const QString work = QDir(QString::fromLocal8Bit(argv[1])).absolutePath();
    QDir().mkpath(work);
    QFile::remove(AppSettings::configDir() + QStringLiteral("/amicloud-desktop.ini"));
    CredentialStore::clear();

    qRegisterMetaType<Account>();
    qRegisterMetaType<SyncRequest>();
    qRegisterMetaType<SyncResult>();
    qRegisterMetaType<AccountInfo>();
    CoreBridge::install();
    Answerer answerer;
    CoreBridge::instance()->setConfirmTarget(&answerer);

    SyncController ctl;
    ctl.start();
    const QString remote = QStringLiteral("/dt_%1").arg(QCoreApplication::applicationPid());
    const QString c1 = work + QStringLiteral("/client1"), c2 = work + QStringLiteral("/client2");

    std::printf("Konto pruefen\n");
    {
        Account good{server, user, pass, true}, bad{server, user, pass + "x", true};
        AccountInfo got;
        int want = 0;
        QObject::connect(&ctl, &SyncController::accountChecked, &ctl,
                         [&](int id, const AccountInfo &i) { if (id == want) { got = i; want = -1; } });
        want = ctl.checkAccount(good);
        CHECK(waitFor([&] { return want == -1; }));
        CHECK(got.ok);
        CHECK(got.user == user);
        want = ctl.checkAccount(bad);
        CHECK(waitFor([&] { return want == -1; }));
        CHECK(!got.ok);
        CHECK(got.httpStatus == 401);
    }

    std::printf("Client 1: erster Abgleich laedt alles hoch\n");
    writeFile(c1 + "/A/a.txt", "alpha");
    writeFile(c1 + "/B/b.txt", "beta");
    writeFile(c1 + "/B/sub/s.txt", "sub");
    writeFile(c1 + "/C/c.txt", "gamma");
    writeFile(c1 + QStringLiteral("/Bücher/ä.txt"), "umlaut");
    CHECK(waitRun(&ctl, [&] { ctl.applySettings(base(server, user, c1, remote), pass, {}); }));
    CHECK(ctl.settings().isConfigured());
    CHECK(QFileInfo(CredentialStore::filePath()).permissions() ==
          (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser |
           QFileDevice::WriteUser));
    CHECK(ctl.state() == SyncController::Idle);
    std::printf("    %s\n", qPrintable(ctl.statusText()));

    std::printf("Ordnerbaum: Liste und Auswahl\n");
    {
        FolderTree tree(&ctl);
        tree.load(ctl.account(), remote, {});
        CHECK(waitFor([&] { return tree.topLevelItem(0) && tree.topLevelItem(0)->childCount() == 4; }));
        QTreeWidgetItem *root = tree.topLevelItem(0);
        QTreeWidgetItem *b = nullptr;
        for (int i = 0; root && i < root->childCount(); i++)
            if (root->child(i)->text(0) == QLatin1String("B")) b = root->child(i);
        CHECK(b != nullptr);
        if (b) {
            b->setCheckState(0, Qt::Unchecked);          // user click: B off
            CHECK(tree.excludes() == QStringList{QStringLiteral("B")});
            b->setExpanded(true);                        // load B's subfolders
            CHECK(waitFor([&] { return b->childCount() == 1 && b->child(0)->text(0) == "sub"; }));
            b->child(0)->setCheckState(0, Qt::Checked);  // B/sub back on: B in, nothing else of B
            CHECK(tree.excludes().isEmpty());            // B's only subfolder is sub
            CHECK(b->checkState(0) == Qt::Checked);
            b->child(0)->setCheckState(0, Qt::Unchecked);
            CHECK(tree.excludes() == QStringList{QStringLiteral("B/sub")});
            CHECK(b->checkState(0) == Qt::PartiallyChecked);
        }
    }

    std::printf("Client 2: Auswahl ohne B\n");
    {
        AppSettings s = base(server, user, c2, remote);
        s.excludes = {QStringLiteral("B")};
        CHECK(waitRun(&ctl, [&] { ctl.applySettings(s, QString(), {}); }));
    }
    CHECK(readFile(c2 + "/A/a.txt") == "alpha");
    CHECK(readFile(c2 + "/C/c.txt") == "gamma");
    CHECK(readFile(c2 + QStringLiteral("/Bücher/ä.txt")) == "umlaut");
    CHECK(!QFileInfo::exists(c2 + "/B"));

    std::printf("Client 2: B wieder aufnehmen\n");
    CHECK(waitRun(&ctl, [&] { ctl.applySettings(base(server, user, c2, remote), QString(), {}); }));
    CHECK(readFile(c2 + "/B/sub/s.txt") == "sub");

    std::printf("Client 2: C abwaehlen, in den Papierkorb\n");
    {
        AppSettings s = base(server, user, c2, remote);
        s.excludes = {QStringLiteral("C")};
        CHECK(waitRun(&ctl, [&] { ctl.applySettings(s, QString(), {c2 + "/C"}); }));
        CHECK(!QFileInfo::exists(c2 + "/C"));
        CHECK(ctl.state() == SyncController::Idle);
    }
    std::printf("Client 1: C ist auf dem Server noch da\n");
    CHECK(waitRun(&ctl, [&] { ctl.applySettings(base(server, user, c1, remote), QString(), {}); }));
    CHECK(readFile(c1 + "/C/c.txt") == "gamma");

    std::printf("Massenloeschung: Rueckfrage, \"Nein\"\n");
    for (int i = 0; i < 30; i++) writeFile(c1 + QStringLiteral("/M/f%1.txt").arg(i), "m");
    CHECK(waitRun(&ctl, [&] { ctl.syncNow(); }));
    QDir(c1 + "/M").removeRecursively();
    answerer.answer = false;
    answerer.asked = 0;
    CHECK(waitRun(&ctl, [&] { ctl.syncNow(); }));
    CHECK(answerer.asked == 1);
    CHECK(waitRun(&ctl, [&] { ctl.syncNow(); }));   // the next run copies them back
    CHECK(QFileInfo::exists(c1 + "/M/f7.txt"));

    std::printf("Pause mitten in einer Uebertragung\n");
    {
        writeFile(c1 + "/big.bin", QByteArray(40 * 1024 * 1024, 'x'));
        bool sawTransfer = false;
        auto c = QObject::connect(&ctl, &SyncController::progressChanged, &ctl, [&] {
            if (ctl.progressText().contains("big.bin") && !sawTransfer) {
                sawTransfer = true;
                ctl.pause();
            }
        });
        waitRun(&ctl, [&] { ctl.syncNow(); });
        QObject::disconnect(c);
        CHECK(sawTransfer);
        CHECK(ctl.state() == SyncController::Paused);
        ctl.syncNow();                                   // ignored while paused
        CHECK(ctl.state() == SyncController::Paused);
        CHECK(waitRun(&ctl, [&] { ctl.resume(); }, 120000));   // starts a run by itself
        CHECK(ctl.state() == SyncController::Idle);
        std::printf("    %s\n", qPrintable(ctl.statusText()));
    }

    std::printf("FSEvents: lokale Aenderung loest einen Abgleich aus, danach Ruhe\n");
    {
        int runs = 0;
        auto c = QObject::connect(&ctl, &SyncController::stateChanged, &ctl, [&] {
            if (ctl.state() == SyncController::Syncing) runs++;
        });
        waitFor([] { return false; }, 2500);   // let the watcher settle after the last run
        int before = runs;
        writeFile(c1 + "/watched.txt", "fsevents");
        CHECK(waitFor([&] { return runs > before && ctl.state() != SyncController::Syncing; }, 30000));
        bool uploaded = false;
        for (const LogEntry &e : ctl.log())
            if (e.text.contains("watched.txt") && e.text.startsWith("Uploaded")) uploaded = true;
        CHECK(uploaded);
        int after = runs;
        waitFor([] { return false; }, 15000);   // own writes: at most one quiet follow-up
        std::printf("    Laeufe durch die Aenderung: %d, danach: %d\n", after - before, runs - after);
        CHECK(runs - after <= 1);
        QObject::disconnect(c);
    }

    std::printf("Papierkorb: auf dem Server geloeschte Datei verschwindet lokal ohne Fehler\n");
    {
        CHECK(waitRun(&ctl, [&] { ctl.applySettings(base(server, user, c2, remote), QString(), {}); }));
        CHECK(QFile::remove(c2 + "/A/a.txt"));
        CHECK(waitRun(&ctl, [&] { ctl.syncNow(); }));
        CHECK(waitRun(&ctl, [&] { ctl.applySettings(base(server, user, c1, remote), QString(), {}); }));
        CHECK(!QFileInfo::exists(c1 + "/A/a.txt"));
        CHECK(ctl.state() == SyncController::Idle);
        std::printf("    %s\n", qPrintable(ctl.statusText()));
    }

    std::printf("App-Passwort falsch: AuthError, keine weiteren Laeufe\n");
    {
        AppSettings s = ctl.settings();
        CHECK(waitRun(&ctl, [&] { ctl.applySettings(s, pass + "falsch", {}); }));
        CHECK(ctl.state() == SyncController::AuthError);
        CHECK(!ctl.lastSync().isNull());
        // a manual retry is allowed (one request), the timer stays off
        CHECK(waitRun(&ctl, [&] { ctl.syncNow(); }));
        CHECK(ctl.state() == SyncController::AuthError);
        // right password again: back to work
        CHECK(waitRun(&ctl, [&] { ctl.applySettings(s, pass, {}); }));
        CHECK(ctl.state() == SyncController::Idle);
    }

    int warnings = 0;
    for (const LogEntry &e : ctl.log())
        if (e.level != UI_INFO || qEnvironmentVariableIsSet("VERBOSE")) { warnings++; std::printf("    [%d] %s\n", e.level, qPrintable(e.text)); }

    ctl.stop();
    std::printf("\n%d ok, %d Fehler\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}

#include "selftest.moc"
