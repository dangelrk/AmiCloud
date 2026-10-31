/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#include "amicloud.h"
#include "config.h"
#include "http.h"
#include "shell.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef PLATFORM_AMIGA
long __stack = 65536; /* 64 KB — VBCC startup.o swaps stack if needed */
const char amiga_ver[] = "$VER: amicloud-shell " AC_VERSION " (" AC_DATE ")";
extern int ac_verbose;
#endif

static void usage(const char *prog)
{
    fprintf(stderr,
        "AmiCloud Shell v" AC_VERSION "\n"
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -s <url>     Server base URL  (e.g. https://host/amicloud)\n"
        "  -u <user>    Username\n"
        "  -p <pass>    Password\n"
        "  -d <path>    Remote base path (default: /)\n"
        "  -c <file>    Config file      (default: ~/.amicloudrc or ENV:amicloud.cfg)\n"
        "  -k           Disable TLS certificate verification (development only)\n"
        "  -v           Verbose HTTP logging\n"
        "\n"
        "Starts an interactive session (cd, lcd, ls, get, put, del, ...\n"
        "type 'help' inside the shell for the full command list).\n",
        prog);
    exit(1);
}

int main(int argc, char **argv)
{
    AcConfig cfg;
    const char *opt_server   = NULL;
    const char *opt_user     = NULL;
    const char *opt_pass     = NULL;
    const char *opt_basepath = NULL;
    const char *opt_cfgfile  = NULL;
    int         opt_noverify = 0;
    int         opt_verbose  = 0;
    int i, rc;

    memset(&cfg, 0, sizeof(cfg));
    cfg.basepath[0] = '/';
    cfg.basepath[1] = '\0';
    cfg.verify_ssl  = 1;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') usage(argv[0]);
        if      (strcmp(argv[i], "-s") == 0 && i+1 < argc) { opt_server   = argv[++i]; }
        else if (strcmp(argv[i], "-u") == 0 && i+1 < argc) { opt_user     = argv[++i]; }
        else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) { opt_pass     = argv[++i]; }
        else if (strcmp(argv[i], "-d") == 0 && i+1 < argc) { opt_basepath = argv[++i]; }
        else if (strcmp(argv[i], "-c") == 0 && i+1 < argc) { opt_cfgfile  = argv[++i]; }
        else if (strcmp(argv[i], "-k") == 0)                { opt_noverify = 1; }
        else if (strcmp(argv[i], "-v") == 0)                { opt_verbose  = 1; }
        else if (strcmp(argv[i], "--help") == 0)            { usage(argv[0]); }
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); usage(argv[0]); }
    }
#ifdef PLATFORM_AMIGA
    if (opt_verbose) ac_verbose = 1;
#else
    (void)opt_verbose;
#endif

    if (opt_cfgfile) {
        config_load(opt_cfgfile, &cfg);
    } else {
        /* Same basename as amicloud's own main.c, so both binaries pick
         * up the same config file without extra setup. */
        char primary[AC_MAX_PATH], fallback[AC_MAX_PATH];
        config_default_path("amicloud", primary, sizeof(primary), fallback, sizeof(fallback));
        if (!config_load(primary, &cfg) && fallback[0]) config_load(fallback, &cfg);
    }
    config_merge(&cfg, opt_server, opt_user, opt_pass, opt_basepath);
    if (opt_noverify) cfg.verify_ssl = 0;

    if (!cfg.server[0])   { fprintf(stderr, "amicloud-shell: server URL required (-s or config file)\n"); usage(argv[0]); }
    if (!cfg.username[0]) { fprintf(stderr, "amicloud-shell: username required (-u or config file)\n");   usage(argv[0]); }
    if (!cfg.password[0]) { fprintf(stderr, "amicloud-shell: password required (-p or config file)\n");   usage(argv[0]); }

    {
        size_t len = strlen(cfg.server);
        if (len > 0 && cfg.server[len-1] == '/') cfg.server[len-1] = '\0';
    }

    if (!http_init()) {
        fprintf(stderr, "amicloud-shell: network initialisation failed\n");
        return 1;
    }

    rc = shell_run(&cfg);
    http_cleanup();
    return rc;
}
