/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
/*
 * main_mui.c — AmiCloud Commander entry point (Amiga/MUI).
 * UNVERIFIED — see the notice in gui.h.
 *
 * Deliberately simpler than the Qt frontend's ConnectDialog for v1: if
 * ENV:amicloud.cfg (or amicloud.cfg in the current directory, same
 * fallback the CLI/shell use) doesn't have a complete server/user/pass,
 * this just reports the problem via MUI_Request and exits rather than
 * building a whole connect-dialog window — one fewer untested MUI window
 * to get right on the first native build. Editing the config file is
 * already the documented way to set these up (amicloud.cfg.example).
 */
#include <libraries/mui.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#include "amicloud.h"
#include "config.h"
#include "http.h"
#include "gui.h"
#include "mui_stubs.h"
#include "progress.h"
#include "wbstartup.h"

#include <string.h>

struct Library       *MUIMasterBase;
struct IntuitionBase *IntuitionBase;

/* http_amiga.c globals — off by default there for CLI/shell compatibility
 * (see their declarations), explicitly configured by configure_debug()
 * below before anything else in the commander runs. */
extern int ac_verbose;
extern int ac_errors_to_stderr;

long __stack = 65536;
const char amiga_ver[] = "$VER: amicloud-commander 0.1 (unverified)";

/* Debug toggle: -v on the shell command line, or a DEBUG tooltype on the
 * Workbench icon (Workbench "Information..." window on
 * amicloud-commander.info) — no other way to turn on HLOG/HERR output,
 * see http_amiga.c. Off by default: ac_errors_to_stderr otherwise starts
 * at 1 (unchanged CLI/shell behaviour), which is exactly what used to pop
 * an unwanted console/output window here the moment any HTTP error
 * fired — so both flags are forced off unless this finds an explicit
 * opt-in.
 *
 * UNVERIFIED Workbench-launch branch: vbcc's AmigaOS startup module is
 * expected to follow the same convention as every other Amiga C runtime
 * (SAS/C, DICE — see mui38dev/MUI/Developer/C/Examples/demo.h's
 * wbmain(): "main(0, (char **)wb_startup)") — i.e. when launched from
 * Workbench, argc is 0 and argv itself, reinterpreted, IS the
 * struct WBStartup pointer (not argv[0]). No native toolchain in this
 * environment to confirm against the actual vbcc startup object linked
 * in (see gui.h) — verify on a real build before relying on it.
 *
 * The actual WBStartup/icon.library dance lives in wbstartup.h/.c
 * (libcloudcore) -- factored out from here and amivcs-gui-mui/
 * amicloud-sync-cx, which each had their own copy of exactly this. */
static void configure_debug(int argc, char *argv[])
{
    int debug;

    if (argc > 0) {
        int i;
        debug = 0;
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-v") == 0) { debug = 1; break; }
        }
    } else {
        debug = ac_wb_tooltype_present(argc, argv, "DEBUG");
    }

    ac_verbose          = debug;
    ac_errors_to_stderr = debug;

    /* Always off here, independent of -v/DEBUG above: this is a windowed
     * GUI app, panel.c's copy engine must never print its "(x/y pct%) "
     * console prefix (see progress.c) — the progress window already shows
     * the same x/y/percent through mui_progress(). */
    ac_console_progress = 0;
}

/* muimaster.library for the MUI object tree, intuition.library for the
 * SetAttrs()/GetAttr() BOOPSI helpers panel_mui.c/gui.c use via MUI's
 * set()/get() macros — both are plain LVO calls through their own base
 * pointer, MUI does not open Intuition for us. */
static int open_libs(void)
{
    MUIMasterBase = OpenLibrary("muimaster.library", 19);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    return MUIMasterBase && IntuitionBase;
}

static void close_libs(void)
{
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (MUIMasterBase) CloseLibrary(MUIMasterBase);
    IntuitionBase = NULL;
    MUIMasterBase = NULL;
}

int main(int argc, char *argv[])
{
    AcConfig cfg;
    int rc;

    configure_debug(argc, argv);

    memset(&cfg, 0, sizeof(cfg));
    cfg.basepath[0] = '/';
    cfg.verify_ssl  = 1;

    if (!config_load("ENV:amicloud.cfg", &cfg))
        config_load("amicloud.cfg", &cfg);

    if (!cfg.server[0] || !cfg.username[0] || !cfg.password[0]) {
        /* No MUI window yet at this point -- OpenLibrary first so
         * MUI_Request has something to talk to. */
        if (open_libs())
            MUI_Request(NULL, NULL, 0, "AmiCloud Commander",
                       "_OK", "ENV:amicloud.cfg fehlt oder ist unvollstaendig "
                              "(server/user/pass) - siehe amicloud.cfg.example.");
        close_libs();
        return 10;
    }
    {
        size_t len = strlen(cfg.server);
        if (len > 0 && cfg.server[len - 1] == '/') cfg.server[len - 1] = '\0';
    }

    if (!open_libs()) {
        close_libs();
        return 20;
    }

    if (!http_init()) {
        close_libs();
        return 1;
    }

    rc = gui_run(&cfg);

    http_cleanup();
    close_libs();
    return rc;
}
