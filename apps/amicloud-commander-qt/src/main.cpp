/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#include "mainwindow.h"
#include "connectdialog.h"

extern "C" {
#include "config.h"
#include "http.h"
#include "progress.h"
/* http_pc.c globals — see their declaration there. Off by default here:
 * this is a windowed GUI app, a curl-level error or panel.c's copy engine
 * must never print straight to a console the user doesn't have (same
 * pattern as amicloud-commander-mui's main_mui.c configure_debug()). -v
 * flips ac_verbose/ac_errors_to_stderr back on for troubleshooting. */
extern int ac_verbose;
extern int ac_errors_to_stderr;
}

#include <QApplication>
#include <QMessageBox>
#include <cstring>
#include <cstdlib>

/* Default config file path — same convention as the CLI/shell (main.c/
 * shell_main.c): ~/.amicloudrc on Unix-like desktops. The GUI only runs
 * on PC platforms, so no Amiga ENV: branch is needed here. */
static QString defaultConfigPath()
{
    const char *home = std::getenv("HOME");
#ifdef _WIN32
    if (!home) home = std::getenv("USERPROFILE");
#endif
    if (home) return QString::fromUtf8(home) + "/.amicloudrc";
    return QStringLiteral(".amicloudrc");
}

int main(int argc, char *argv[])
{
    /* Off unless -v is passed — see the extern declarations above. Checked
     * before QApplication touches argv so "-v" doesn't need to also be a
     * recognized Qt option. */
    ac_console_progress = 0;
    ac_errors_to_stderr = 0;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
            ac_verbose          = 1;
            ac_errors_to_stderr = 1;
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName("AmiCloud Commander");
    QApplication::setApplicationVersion(AC_VERSION);

    AcConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.basepath[0] = '/';
    cfg.verify_ssl  = 1;

    QByteArray cfgPath = defaultConfigPath().toUtf8();
    config_load(cfgPath.constData(), &cfg);

    if (!cfg.server[0] || !cfg.username[0] || !cfg.password[0]) {
        ConnectDialog dlg(cfg);
        if (dlg.exec() != QDialog::Accepted) return 0;
        cfg = dlg.result();
    }
    {
        size_t len = std::strlen(cfg.server);
        if (len > 0 && cfg.server[len - 1] == '/') cfg.server[len - 1] = '\0';
    }

    if (http_init() == 0) {
        const char *why = http_last_error();
        QMessageBox::critical(nullptr, QApplication::applicationName(),
            QObject::tr("Netzwerk-Initialisierung fehlgeschlagen%1")
                .arg(*why ? QStringLiteral(":\n") + QString::fromUtf8(why)
                          : QStringLiteral(".")));
        return 1;
    }

    MainWindow window(cfg);
    window.show();

    int rc = app.exec();
    http_cleanup();
    return rc;
}
