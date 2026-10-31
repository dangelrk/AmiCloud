/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
/*
 * amicloud-sync — one-shot CLI front-end for libcloudsync's cloudsync_run_ex()
 * (docs/design/05_Refactoring_Client_Handler.md, M5). Placeholder for the
 * roadmap's Phase 5 "Sync Commodity" (docs/design/01_Roadmap_Refactoring.md):
 * this is a plain one-shot invocation, not a background daemon watching
 * folders — that's the gap left for a future iteration to fill. It
 * exists now so cloudsync_run_ex() has a real, runnable consumer instead of
 * sitting completely unlinked.
 *
 * Same config-loading convention as apps/amicloud's main.c
 * (server/user/pass from -s/-u/-p or a config file, ENV:amicloud.cfg on
 * Amiga / ~/.amicloudrc on Unix / amicloud.cfg on Windows).
 */
#include "amicloud.h"
#include "config.h"
#include "http.h"
#include "sync.h"
#include "amicloud_transport.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef PLATFORM_AMIGA
long __stack = 65536;
const char amiga_ver[] = "$VER: amicloud-sync " AC_VERSION " (" AC_DATE ")";
#endif

static void usage(const char *prog)
{
    fprintf(stderr,
        "amicloud-sync v" AC_VERSION " — one-shot local<->cloud folder sync\n"
        "Usage: %s [options] <local-dir> <remote-dir>\n"
        "\n"
        "Options:\n"
        "  -s <url>     Server base URL\n"
        "  -u <user>    Username\n"
        "  -p <pass>    Password\n"
        "  -c <file>    Config file (default: ~/.amicloudrc or ENV:amicloud.cfg)\n"
        "  -k           Disable TLS certificate verification (development only)\n"
        "  -n           Dry run — show what would happen, change nothing\n",
        prog);
    exit(1);
}

int main(int argc, char **argv)
{
    AcConfig cfg;
    const char *opt_server = NULL, *opt_user = NULL, *opt_pass = NULL, *opt_cfgfile = NULL;
    int opt_noverify = 0, opt_dryrun = 0;
    int i;
    const char *local_root, *remote_root;
    Progress    pr;
    SyncSummary sum;
    SyncTransport *t;

    memset(&cfg, 0, sizeof(cfg));
    cfg.basepath[0] = '/';
    cfg.basepath[1] = '\0';
    cfg.verify_ssl  = 1;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if      (strcmp(argv[i], "-s") == 0 && i+1 < argc) { opt_server  = argv[++i]; }
        else if (strcmp(argv[i], "-u") == 0 && i+1 < argc) { opt_user    = argv[++i]; }
        else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) { opt_pass    = argv[++i]; }
        else if (strcmp(argv[i], "-c") == 0 && i+1 < argc) { opt_cfgfile = argv[++i]; }
        else if (strcmp(argv[i], "-k") == 0)               { opt_noverify = 1; }
        else if (strcmp(argv[i], "-n") == 0)               { opt_dryrun   = 1; }
        else if (strcmp(argv[i], "--help") == 0)           { usage(argv[0]); }
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); usage(argv[0]); }
    }

    if (opt_cfgfile) {
        config_load(opt_cfgfile, &cfg);
    } else {
        /* Same basename as amicloud's own main.c -- shares its config
         * file convention, see this file's header comment. */
        char primary[AC_MAX_PATH], fallback[AC_MAX_PATH];
        config_default_path("amicloud", primary, sizeof(primary), fallback, sizeof(fallback));
        if (!config_load(primary, &cfg) && fallback[0]) config_load(fallback, &cfg);
    }
    config_merge(&cfg, opt_server, opt_user, opt_pass, NULL);
    if (opt_noverify) cfg.verify_ssl = 0;

    if (i + 1 >= argc) { fprintf(stderr, "amicloud-sync: <local-dir> and <remote-dir> required\n"); usage(argv[0]); }
    local_root  = argv[i++];
    remote_root = argv[i];

    if (!cfg.server[0])   { fprintf(stderr, "amicloud-sync: server URL required (-s or config file)\n"); usage(argv[0]); }
    if (!cfg.username[0]) { fprintf(stderr, "amicloud-sync: username required (-u or config file)\n");   usage(argv[0]); }
    if (!cfg.password[0]) { fprintf(stderr, "amicloud-sync: password required (-p or config file)\n");   usage(argv[0]); }

    {
        size_t len = strlen(cfg.server);
        if (len > 0 && cfg.server[len-1] == '/') cfg.server[len-1] = '\0';
    }

    if (!http_init()) {
        fprintf(stderr, "amicloud-sync: network initialisation failed\n");
        return 1;
    }

    t = amicloud_transport_open(&cfg);
    if (!t) {
        fprintf(stderr, "amicloud-sync: out of memory\n");
        http_cleanup();
        return 1;
    }

    progress_reset(&pr);
    {
        SyncOptions o;
        sync_options_init(&o);
        o.dry_run = opt_dryrun;
        o.verbose = 1;
        cloudsync_run_ex(t, local_root, remote_root, &o, &pr, &sum);
    }

    amicloud_transport_close(t);
    http_cleanup();
    return sum.errors ? 1 : 0;
}
