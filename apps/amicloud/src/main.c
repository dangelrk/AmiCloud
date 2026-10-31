/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#include "amicloud.h"
#include "config.h"
#include "http.h"
#include "cmd.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef PLATFORM_AMIGA
long __stack = 65536; /* 64 KB — VBCC startup.o swaps stack if needed */
const char amiga_ver[] = "$VER: amicloud " AC_VERSION " (" AC_DATE ")";
extern int ac_verbose;
#endif

/* Thin printer around cmd_search(): one full path per line, directories
 * suffixed with '/' (like cmd_list()'s LIST_NAMES mode). */
static int cmd_search_print(const AcConfig *cfg, const char *root, const char *pattern)
{
    AcSearchHit *hits;
    int n, i, truncated;

    if (!cmd_search(cfg, root, pattern, 1, &hits, &n, &truncated)) return 1;

    for (i = 0; i < n; i++) {
        printf("%s%s\n", hits[i].path, hits[i].is_dir ? "/" : "");
    }
    if (truncated) {
        fprintf(stderr, "amicloud: search: results truncated (server limit reached)\n");
    }
    cmd_search_free(hits);
    return 0;
}

/* -f/--force/FORCE after get/put: overwrite an existing destination
 * without asking (same spellings as amicloud-shell's flag). */
static int is_force_flag(const char *t)
{
    return strcmp(t, "-f") == 0 || strcmp(t, "--force") == 0 ||
           strcmp(t, "FORCE") == 0 || strcmp(t, "force") == 0;
}

int main(int argc, char **argv)
{
    AcConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.basepath[0] = '/';
    cfg.basepath[1] = '\0';
    cfg.verify_ssl  = 1;

    const char *opt_server   = NULL;
    const char *opt_user     = NULL;
    const char *opt_pass     = NULL;
    const char *opt_basepath = NULL;
    const char *opt_cfgfile  = NULL;
    int         opt_noverify = 0;
    int         opt_verbose  = 0;

    int i;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') break; /* end of options */
        if      (strcmp(argv[i], "-s") == 0 && i+1 < argc) { opt_server   = argv[++i]; }
        else if (strcmp(argv[i], "-u") == 0 && i+1 < argc) { opt_user     = argv[++i]; }
        else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) { opt_pass     = argv[++i]; }
        else if (strcmp(argv[i], "-d") == 0 && i+1 < argc) { opt_basepath = argv[++i]; }
        else if (strcmp(argv[i], "-c") == 0 && i+1 < argc) { opt_cfgfile  = argv[++i]; }
        else if (strcmp(argv[i], "-k") == 0)                { opt_noverify = 1; }
        else if (strcmp(argv[i], "-v") == 0)                { opt_verbose  = 1; }
        else if (strcmp(argv[i], "--help") == 0)            { config_usage(argv[0]); }
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); config_usage(argv[0]); }
    }
#ifdef PLATFORM_AMIGA
    if (opt_verbose) ac_verbose = 1;
#endif

    /* Load config file (command-line args override) */
    if (opt_cfgfile) {
        config_load(opt_cfgfile, &cfg);
    } else {
        char primary[AC_MAX_PATH], fallback[AC_MAX_PATH];
        config_default_path("amicloud", primary, sizeof(primary), fallback, sizeof(fallback));
        if (!config_load(primary, &cfg) && fallback[0]) config_load(fallback, &cfg);
    }
    config_merge(&cfg, opt_server, opt_user, opt_pass, opt_basepath);
    if (opt_noverify) cfg.verify_ssl = 0;

    if (i >= argc) { fprintf(stderr, "amicloud: no command given\n"); config_usage(argv[0]); }
    const char *cmd = argv[i++];

    if (strcmp(cmd, "version") == 0) {
        printf("AmiCloud v" AC_VERSION "\n");
        return 0;
    }

    /* Validate we have the minimum required config */
    if (!cfg.server[0])   { fprintf(stderr, "amicloud: server URL required (-s or config file)\n"); config_usage(argv[0]); }
    if (!cfg.username[0]) { fprintf(stderr, "amicloud: username required (-u or config file)\n");   config_usage(argv[0]); }
    if (!cfg.password[0]) { fprintf(stderr, "amicloud: password required (-p or config file)\n");   config_usage(argv[0]); }

    /* Remove trailing slash from server URL */
    {
        size_t len = strlen(cfg.server);
        if (len > 0 && cfg.server[len-1] == '/') cfg.server[len-1] = '\0';
    }

    if (!http_init()) {
        fprintf(stderr, "amicloud: network initialisation failed\n");
        return 1;
    }

    int rc = 0;

    if (strcmp(cmd, "list") == 0) {
        rc = cmd_list(&cfg, (i < argc) ? argv[i] : NULL);

    } else if (strcmp(cmd, "get") == 0) {
        int force = 0;
        if (i < argc && is_force_flag(argv[i])) { force = 1; i++; }
        if (i >= argc) { fprintf(stderr, "amicloud: get requires <remote>\n"); rc = 1; }
        else {
            const char *remote = argv[i++];
            const char *local  = (i < argc) ? argv[i] : NULL;
            rc = cmd_get_checked(&cfg, remote, local, force);
        }

    } else if (strcmp(cmd, "put") == 0) {
        int force = 0;
        if (i < argc && is_force_flag(argv[i])) { force = 1; i++; }
        if (i >= argc) { fprintf(stderr, "amicloud: put requires <local>\n"); rc = 1; }
        else {
            const char *local  = argv[i++];
            const char *remote = (i < argc) ? argv[i] : NULL;
            rc = cmd_put_checked(&cfg, local, remote, force);
        }

    } else if (strcmp(cmd, "del") == 0) {
        if (i >= argc) { fprintf(stderr, "amicloud: del requires <remote>\n"); rc = 1; }
        else rc = cmd_del(&cfg, argv[i]);

    } else if (strcmp(cmd, "info") == 0) {
        if (i >= argc) { fprintf(stderr, "amicloud: info requires <remote>\n"); rc = 1; }
        else rc = cmd_info(&cfg, argv[i]);

    } else if (strcmp(cmd, "mkdir") == 0) {
        if (i >= argc) { fprintf(stderr, "amicloud: mkdir requires <path>\n"); rc = 1; }
        else rc = cmd_mkdir(&cfg, argv[i]);

    } else if (strcmp(cmd, "rename") == 0) {
        if (i + 1 >= argc) { fprintf(stderr, "amicloud: rename requires <old> <new>\n"); rc = 1; }
        else rc = cmd_rename(&cfg, argv[i], argv[i + 1]);

    } else if (strcmp(cmd, "search") == 0 || strcmp(cmd, "find") == 0) {
        if (i >= argc) { fprintf(stderr, "amicloud: search requires <pattern>\n"); rc = 1; }
        else {
            const char *pat  = argv[i++];
            const char *root = (i < argc) ? argv[i] : NULL; /* NULL = basepath/"/" */
            rc = cmd_search_print(&cfg, root, pat);
        }

    } else if (strcmp(cmd, "quota") == 0 || strcmp(cmd, "df") == 0) {
        rc = cmd_quota(&cfg);

    } else {
        fprintf(stderr, "amicloud: unknown command '%s'\n", cmd);
        config_usage(argv[0]);
    }

    http_cleanup();
    return rc;
}
