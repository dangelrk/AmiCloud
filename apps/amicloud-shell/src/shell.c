/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
/*
 * shell.c — interactive REPL for amicloud-shell.
 * See docs/interactive-shell-plan.html for the design this implements.
 */
#include "shell.h"
#include "amicloud.h"
#include "cmd.h"
#include "localfs.h"
#include "lineedit.h"
#include "history.h"
#include "pattern.h"
#include "md5.h"
#include "ui_hooks.h"
#include "panel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_TOKENS 16

typedef struct {
    AcConfig base_cfg;              /* as loaded at startup, never mutated */
    char     remote_cwd[AC_MAX_PATH];
    char     local_start_cwd[AC_MAX_PATH]; /* process cwd when the shell started */
} ShellSession;

/* ------------------------------------------------------------------ */
/* Small portable helpers                                              */
/* ------------------------------------------------------------------ */

/* Our own case-insensitive compare: strcasecmp/stricmp aren't C99 and
 * aren't uniformly available across VBCC/MinGW/gcc, tolower() is. */
static int ac_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static const char *platform_name(void)
{
#ifdef PLATFORM_AMIGA
    return "AmigaOS";
#elif defined(PLATFORM_WIN)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

/* ------------------------------------------------------------------ */
/* Tokenizer — whitespace-separated, "..." allows spaces in a token     */
/* ------------------------------------------------------------------ */
static int tokenize(char *line, char *tokens[], int max)
{
    int n = 0;
    char *p = line;

    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (*p == '"') {
            p++;
            tokens[n++] = p;
            while (*p && *p != '"') p++;
            if (*p) { *p = '\0'; p++; }
        } else {
            tokens[n++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) { *p = '\0'; p++; }
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Flags: -r/--recursive/RECURSIVE and -f/--force/FORCE, in any order   */
/* and mixed with positional arguments (see plan §8.1).                 */
/* ------------------------------------------------------------------ */
typedef struct {
    int   recursive;
    int   force;
    char *pos[8];
    int   npos;
} ParsedArgs;

static int is_recursive_flag(const char *t)
{
    return strcmp(t, "-r") == 0 || strcmp(t, "--recursive") == 0 ||
           ac_strcasecmp(t, "recursive") == 0 || ac_strcasecmp(t, "all") == 0;
}

static int is_force_flag(const char *t)
{
    return strcmp(t, "-f") == 0 || strcmp(t, "--force") == 0 ||
           ac_strcasecmp(t, "force") == 0;
}

static void parse_args(char **tokens, int n, ParsedArgs *pa)
{
    int i;
    memset(pa, 0, sizeof(*pa));
    for (i = 0; i < n; i++) {
        if (is_recursive_flag(tokens[i])) pa->recursive = 1;
        else if (is_force_flag(tokens[i])) pa->force = 1;
        else if (pa->npos < (int)(sizeof(pa->pos) / sizeof(pa->pos[0])))
            pa->pos[pa->npos++] = tokens[i];
    }
}

/* Separators of a LOCAL path on this platform: '/' everywhere, the
 * AmigaDOS volume/device colon on the Amiga, '\\' and the drive colon on
 * Windows. On macOS/Linux '\\' and ':' are ordinary name characters.
 * Remote paths only ever use '/'. */
static int is_local_sep(char c)
{
    if (c == '/') return 1;
#if defined(PLATFORM_AMIGA)
    if (c == ':') return 1;
#elif defined(PLATFORM_WIN)
    if (c == '\\' || c == ':') return 1;
#endif
    return 0;
}

/* Last separator in path (local or remote rules, see above), or NULL. */
static const char *last_sep(const char *path, int local)
{
    const char *p, *last = NULL;
    for (p = path; *p; p++)
        if (local ? is_local_sep(*p) : (*p == '/')) last = p;
    return last;
}

/* Zerlegt z.B. "sub" + Muster "?.txt" (Pfad "sub/?.txt") in
 * dir="sub" (dir="" wenn kein Verzeichnisanteil) und liefert einen Zeiger
 * auf den Musteranteil (letztes Segment) INNERHALB von path zurueck. NULL,
 * wenn das letzte Segment kein Wildcard-Zeichen enthaelt — dann bleibt dir
 * unveraendert und der Aufrufer soll den kompletten Original-String
 * weiterverwenden (bisheriges Verhalten). local: path ist ein lokaler
 * Pfad (Trennzeichen je System, siehe is_local_sep()), sonst Remote. */
static const char *split_pattern(const char *path, int local, char *dir, size_t dirsz)
{
    const char *sep  = last_sep(path, local);
    const char *last = sep ? sep + 1 : path;

    if (!pattern_is_wildcard(last)) return NULL;

    if (sep) {
        /* The separator stays part of dir when it is the root or ends a
         * volume/drive ("/", "DH0:", "C:", "C:\\") -- dropping it would
         * name the current directory instead. */
        size_t len = (size_t)(sep - path);
        if (len == 0 || *sep == ':' || path[len - 1] == ':') len++;
        if (len >= dirsz) len = dirsz - 1;
        memcpy(dir, path, len);
        dir[len] = '\0';
    } else {
        dir[0] = '\0';
    }
    return last;
}

/* Render a canonical '/'-rooted remote path in the platform-native style
 * used for the prompt and `pwd` (plan §6.1/§8.2): ':' root on Amiga, '\'
 * root+separators on Windows, '/' root+separators elsewhere. Purely
 * cosmetic — never sent to the server. */
static void render_remote_display(char *out, size_t outsz, const char *canon)
{
#ifdef PLATFORM_AMIGA
    char root = ':';
#elif defined(PLATFORM_WIN)
    char root = '\\';
#else
    char root = '/';
#endif
    if (strcmp(canon, "/") == 0) {
        out[0] = root; out[1] = '\0';
        return;
    }
    {
        const char *p = canon + 1; /* skip the canonical leading '/' */
        size_t n = 0;
        out[n++] = root;
        for (; *p && n + 1 < outsz; p++) {
#ifdef PLATFORM_WIN
            out[n++] = (*p == '/') ? '\\' : *p;
#else
            out[n++] = *p;
#endif
        }
        out[n] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* eff_config(): the trick that lets us reuse cmd_*() unmodified — build */
/* a config copy whose basepath is the *current* remote directory, so    */
/* full_path()'s existing relative-path handling does the rest.          */
/* ------------------------------------------------------------------ */
static void eff_config(const ShellSession *s, AcConfig *eff)
{
    *eff = s->base_cfg;
    strncpy(eff->basepath, s->remote_cwd, sizeof(eff->basepath) - 1);
    eff->basepath[sizeof(eff->basepath) - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/* Banner & prompt                                                      */
/* ------------------------------------------------------------------ */
static void print_banner(const ShellSession *s)
{
    char version[32] = "";
    char localcwd[AC_MAX_PATH];
    int online;

    printf("AmiCloud Shell v" AC_VERSION " (%s)\n", platform_name());

    online = cmd_status(&s->base_cfg, version, sizeof(version), NULL);
    if (online)
        printf("Server   : %s  (Server v%s, TLS %s)\n",
               s->base_cfg.server, version, s->base_cfg.verify_ssl ? "ok" : "off");
    else
        printf("Server   : %s  (WARNUNG: nicht erreichbar oder Health-Check nicht unterstuetzt)\n",
               s->base_cfg.server);

    printf("User     : %s\n", s->base_cfg.username);
    printf("Remote   : %s\n", s->remote_cwd);
    if (localfs_getcwd(localcwd, sizeof(localcwd)))
        printf("Local    : %s\n", localcwd);

    printf("Type 'help' for a list of commands, 'quit' to exit.\n");
}

static void build_prompt(const ShellSession *s, char *out, size_t outsz)
{
    char disp[AC_MAX_PATH];
    render_remote_display(disp, sizeof(disp), s->remote_cwd);
    snprintf(out, outsz, "%s> ", disp);
}

static void print_prompt(const ShellSession *s)
{
    char prompt[AC_MAX_PATH + 8];
    build_prompt(s, prompt, sizeof(prompt));
    fputs(prompt, stdout);
    fflush(stdout);
}

/* Zeilenbreite bewusst auf ca. 70 Zeichen gehalten (schmale Amiga-Konsolen).
 * Lange Flag-Synonyme (-r/--recursive/RECURSIVE, -f/--force/FORCE) stehen
 * nur einmal in der Kopfzeile statt bei jedem Befehl wiederholt. */
static void print_help(void)
{
    printf(
        "Commands (Flags: -r=--recursive=RECURSIVE  -f=--force=FORCE):\n"
        "  help                        diese Uebersicht\n"
        "  pwd / lpwd                  Remote-/lokales Verzeichnis zeigen\n"
        "  cd [pfad]                   Remote-Verzeichnis wechseln\n"
        "  lcd [pfad]                  lokal wechseln (Kurzformen: .. : \\ /)\n"
        "  ls|dir|list [pfad] [-n|-s|-d|-m]\n"
        "                               Verzeichnis auflisten\n"
        "                               (-n Namen, -s +Groesse, -d +Datum, -m +MD5)\n"
        "  lls|ldir|llist [pfad] [-n|-s|-d|-m]\n"
        "                               lokal auflisten (gleiche Flags wie ls;\n"
        "                               -m rechnet MD5 frisch, kein Cache)\n"
        "  get [-r] [-f] <remote/muster> [lokal]\n"
        "                               Datei/Ordner herunterladen\n"
        "  put [-r] [-f] <lokal/muster> [remote]\n"
        "                               Datei/Ordner hochladen\n"
        "                               (Muster: * ? sowie #? ? ueberall;\n"
        "                               fragt vor dem Ueberschreiben,\n"
        "                               -f ueberschreibt ohne Frage)\n"
        "  del|rm|delete [-r|-f] <pfad>\n"
        "                               loeschen (-r rekursiv, -f erzwingt)\n"
        "  mkdir|makedir <pfad>         Verzeichnis anlegen\n"
        "  rename|mv <alt> <neu>        umbenennen/verschieben\n"
        "  info|stat <pfad>             Metadaten anzeigen\n"
        "  search|find <muster> [pfad]  Datei/Ordner suchen (* ? #?, rekursiv;\n"
        "                               Standard-Scope: aktuelles Verzeichnis)\n"
        "  md5 <remote>                 MD5-Pruefsumme einer Remote-Datei\n"
        "  lmd5 <lokal>                 MD5-Pruefsumme einer lokalen Datei\n"
        "  history [clear]              Verlauf anzeigen/leeren\n"
        "  quit|exit|bye                Shell beenden\n"
    );
}

/* ------------------------------------------------------------------ */
/* Recursive get/put orchestration (local+remote, so it lives here and  */
/* not in cmd.c — see plan §9.1/§9.2).                                  */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Command handlers                                                     */
/* ------------------------------------------------------------------ */
static void do_cd(ShellSession *s, const char *arg)
{
    char target[AC_MAX_PATH];

    if (!arg || !*arg) {
        strncpy(target, s->base_cfg.basepath, sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';
    } else {
        remote_resolve(target, sizeof(target), s->remote_cwd, arg);
    }

    if (!cmd_is_dir(&s->base_cfg, target)) {
        fprintf(stderr, "amicloud: cd: %s: not a directory or not found\n", target);
        return;
    }
    strncpy(s->remote_cwd, target, sizeof(s->remote_cwd) - 1);
    s->remote_cwd[sizeof(s->remote_cwd) - 1] = '\0';
}

static void do_lcd(ShellSession *s, const char *arg)
{
    const char *target;

    if (!arg || !*arg) {
        if (!s->local_start_cwd[0]) {
            fprintf(stderr, "amicloud: lcd: no starting directory recorded\n");
            return;
        }
        target = s->local_start_cwd;
    } else if (strcmp(arg, "..") == 0) {
#ifdef PLATFORM_AMIGA
        target = "/"; /* Amiga has no native ".."; "/" is its "up one" */
#else
        target = "..";
#endif
    } else if (strcmp(arg, ":") == 0 || strcmp(arg, "\\") == 0) {
#ifdef PLATFORM_AMIGA
        target = ":";
#elif defined(PLATFORM_WIN)
        target = "\\";
#else
        target = "/";
#endif
    } else {
        target = arg; /* '/' and ordinary paths are native everywhere already */
    }

    if (!localfs_chdir(target))
        fprintf(stderr, "amicloud: lcd: cannot change to '%s'\n", arg);
}

/* -n/--names/NAMES, -s/--size/SIZE, -d/--dates/DATES — sich gegenseitig
 * ausschliessende Anzeige-Flags (nicht ueber ParsedArgs, da die dortigen
 * recursive/force-Booleans ein anderes Konzept sind: hier ist genau EIN
 * Modus aktiv, keine Kombination). Letztes gefundenes Flag gewinnt. */
static ListMode parse_list_mode(char **tok, int n)
{
    ListMode mode = LIST_FULL;
    int i;
    for (i = 0; i < n; i++) {
        if (strcmp(tok[i], "-n") == 0 || strcmp(tok[i], "--names") == 0 ||
            ac_strcasecmp(tok[i], "NAMES") == 0)
            mode = LIST_NAMES;
        else if (strcmp(tok[i], "-s") == 0 || strcmp(tok[i], "--size") == 0 ||
                 ac_strcasecmp(tok[i], "SIZE") == 0)
            mode = LIST_SIZE;
        else if (strcmp(tok[i], "-d") == 0 || strcmp(tok[i], "--dates") == 0 ||
                 ac_strcasecmp(tok[i], "DATES") == 0)
            mode = LIST_DATE;
        else if (strcmp(tok[i], "-m") == 0 || strcmp(tok[i], "--md5") == 0 ||
                 ac_strcasecmp(tok[i], "MD5") == 0)
            mode = LIST_MD5;
    }
    return mode;
}

static void do_ls(ShellSession *s, char **tok, int n)
{
    AcConfig eff;
    ListMode mode = parse_list_mode(tok, n);
    const char *path = NULL;
    int i;

    /* Erstes Token, das kein Anzeige-Flag ist, ist der Pfad. */
    for (i = 0; i < n; i++) {
        if (tok[i][0] == '-' || ac_strcasecmp(tok[i], "NAMES") == 0 ||
            ac_strcasecmp(tok[i], "SIZE") == 0 || ac_strcasecmp(tok[i], "DATES") == 0 ||
            ac_strcasecmp(tok[i], "MD5") == 0)
            continue;
        path = tok[i];
        break;
    }

    eff_config(s, &eff);
    cmd_list_mode(&eff, path, mode);
}

/* Lokale Auflistung: gleiche Spaltenreihenfolge und dieselben Anzeige-Modi
 * wie die Remote-Auflistung (Name -> Size -> Modified -> Type).
 *
 * LIST_MD5 (v4.1, plan §18.4) ist bewusst kein Bestandteil von LIST_FULL:
 * die Pruefsumme wird HIER FRISCH BERECHNET (kein lokaler Cache), kostet
 * also einen vollen Lesedurchlauf pro Datei — ploetzlich langsames `lls`
 * waere eine boese Ueberraschung. Nur mit explizitem -m/--md5/MD5-Flag. */
static void do_lls(char **tok, int n)
{
    LocalDir *d;
    LocalDirEntry e;
    ListMode    mode = parse_list_mode(tok, n);
    const char *path = NULL;
    int i;

    for (i = 0; i < n; i++) {
        if (tok[i][0] == '-' || ac_strcasecmp(tok[i], "NAMES") == 0 ||
            ac_strcasecmp(tok[i], "SIZE") == 0 || ac_strcasecmp(tok[i], "DATES") == 0 ||
            ac_strcasecmp(tok[i], "MD5") == 0)
            continue;
        path = tok[i];
        break;
    }
    if (!path || !*path) path = ".";

    d = localfs_opendir(path);
    if (!d) { fprintf(stderr, "amicloud: lls: cannot open %s\n", path); return; }

    if (mode == LIST_FULL)
        printf("%-28s  %9s  %-19s  %s\n", "Name", "Size", "Modified", "Type");
    else if (mode == LIST_SIZE)
        printf("%-28s  %9s\n", "Name", "Size");
    else if (mode == LIST_DATE)
        printf("%-28s  %-19s\n", "Name", "Modified");
    else if (mode == LIST_MD5)
        printf("%-28s  %s\n", "Name", "MD5");

    while (localfs_readdir(d, &e)) {
        switch (mode) {
        case LIST_NAMES:
            printf("%s%s\n", e.name, e.is_dir ? "/" : "");
            break;
        case LIST_SIZE:
            printf("%-28s  %9ld\n", e.name, e.is_dir ? 0L : e.size);
            break;
        case LIST_DATE:
            printf("%-28s  %-19s\n", e.name, e.modified);
            break;
        case LIST_MD5:
            if (e.is_dir) {
                printf("%-28.28s  -\n", e.name);
            } else {
                char full[AC_MAX_PATH], hex[33];
                localfs_join(full, sizeof(full), path, e.name);
                if (md5_file_hex(full, hex))
                    printf("%-28.28s  %s\n", e.name, hex);
                else
                    printf("%-28.28s  (Fehler)\n", e.name);
            }
            fflush(stdout); /* zeilenweise sichtbar waehrend des Rechnens */
            break;
        case LIST_FULL:
        default:
            printf("%-28.28s  %9ld  %-19s  %s\n", e.name,
                   e.is_dir ? 0L : e.size, e.modified,
                   e.is_dir ? "[dir]" : "file");
            break;
        }
    }
    localfs_closedir(d);
}

/* Overwrite-prompt metadata for the wildcard copies below (the same
 * FileMeta panel_get_dir()/panel_put_dir() build for -r). */
static void meta_from_entry(FileMeta *m, const AcEntry *ae)
{
    m->size = ae->size;
    strncpy(m->date, ae->modified, sizeof(m->date) - 1);
    m->date[sizeof(m->date) - 1] = '\0';
    strncpy(m->md5, ae->md5, sizeof(m->md5) - 1);
    m->md5[sizeof(m->md5) - 1] = '\0';
}

static const AcEntry *find_remote_file(const AcEntry *entries, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++)
        if (!entries[i].is_dir && strcmp(entries[i].name, name) == 0) return &entries[i];
    return NULL;
}

/* 1 + *m filled if dir contains a plain file called name. */
static int local_file_meta(const char *dir, const char *name, FileMeta *m)
{
    LocalDir *d = localfs_opendir(dir);
    LocalDirEntry e;
    int found = 0;
    if (!d) return 0;
    while (localfs_readdir(d, &e)) {
        if (!e.is_dir && strcmp(e.name, name) == 0) {
            m->size = e.size;
            strncpy(m->date, e.modified, sizeof(m->date) - 1);
            m->date[sizeof(m->date) - 1] = '\0';
            m->md5[0] = '\0';
            found = 1;
            break;
        }
    }
    localfs_closedir(d);
    return found;
}

/* Wildcard-Zweig von `get`: pat/dirpart kommen bereits aus split_pattern().
 * Listet das Remote-Verzeichnis dirpart, holt jeden Treffer einzeln
 * (Verzeichnisse nur mit -r, sonst Warnung + Ueberspringen). */
static void do_get_pattern(const AcConfig *eff,
                          const char *dirpart, const char *pat,
                          const char *local_arg, int recursive, int force)
{
    char     rdir[AC_MAX_PATH], ldestdir[AC_MAX_PATH];
    Pattern *p;
    AcEntry *entries;
    int      i, ne, matches = 0;
    CopyCtx  c; /* ask = !force: prompts via ui_overwrite() on conflicts */

    cmd_resolve_path(eff, dirpart, rdir, sizeof(rdir));
    strncpy(ldestdir, (local_arg && *local_arg) ? local_arg : ".",
            sizeof(ldestdir) - 1);
    ldestdir[sizeof(ldestdir) - 1] = '\0';

    p = pattern_compile(pat);
    if (!p) { fprintf(stderr, "amicloud: get: ungueltiges Muster '%s'\n", pat); return; }

    if (!cmd_list_raw(eff, rdir, &entries, &ne)) {
        fprintf(stderr, "amicloud: get: kann %s nicht auflisten\n", rdir);
        pattern_free(p);
        return;
    }
    localfs_mkdir_p(ldestdir);

    /* Pass 1: count matches (+ recurse-count matched directories) so y
     * and total_bytes are known before the first transfer (plan §17.1). */
    copyctx_init(&c, !force);
    for (i = 0; i < ne; i++) {
        if (!pattern_match(p, entries[i].name)) continue;
        matches++;
        if (entries[i].is_dir) {
            if (recursive) {
                char rchild[AC_MAX_PATH];
                snprintf(rchild, sizeof(rchild), "%s/%s", rdir, entries[i].name);
                count_remote_tree(eff, rchild, &c.pr);
            }
        } else {
            c.pr.total++;
            c.pr.bytes_total += entries[i].size;
        }
    }

    /* Pass 2: transfer. */
    for (i = 0; i < ne; i++) {
        char rchild[AC_MAX_PATH], lchild[AC_MAX_PATH];
        if (!pattern_match(p, entries[i].name)) continue;
        snprintf(rchild, sizeof(rchild), "%s/%s", rdir, entries[i].name);
        localfs_join(lchild, sizeof(lchild), ldestdir, entries[i].name);
        if (entries[i].is_dir) {
            if (recursive) panel_get_dir(eff, rchild, lchild, &c);
            else fprintf(stderr,
                    "amicloud: get: '%s' ist ein Verzeichnis (-r verwenden)\n",
                    entries[i].name);
        } else {
            FileMeta smeta, dmeta;
            int have_dst = c.ask && local_file_meta(ldestdir, entries[i].name, &dmeta);
            meta_from_entry(&smeta, &entries[i]);
            panel_copy_file(PANEL_CLOUD, eff, rchild, PANEL_LOCAL, NULL, lchild,
                            entries[i].name, &smeta, have_dst ? &dmeta : NULL, &c);
        }
        if (c.cancelled) { printf("Abgebrochen.\n"); break; }
    }
    cmd_entries_free(entries);
    pattern_free(p);

    if (matches == 0) printf("Keine Treffer fuer '%s'\n", pat);
    else               printf("%d Datei(en) ok, %d Fehler\n", c.pr.ok, c.pr.err);
}

static void do_get(ShellSession *s, char **tok, int n)
{
    ParsedArgs pa;
    AcConfig eff;
    char dirpart[AC_MAX_PATH];
    const char *pat;
    parse_args(tok, n, &pa);

    if (pa.npos < 1) { fprintf(stderr, "amicloud: get requires <remote> [local]\n"); return; }
    eff_config(s, &eff);

    pat = split_pattern(pa.pos[0], 0, dirpart, sizeof(dirpart));
    if (pat) {
        do_get_pattern(&eff, dirpart, pat,
                       pa.npos > 1 ? pa.pos[1] : NULL, pa.recursive, pa.force);
        return;
    }

    if (pa.recursive) {
        char rfull[AC_MAX_PATH], lbuf[AC_MAX_PATH];
        CopyCtx c; /* ask = !force: prompts via ui_overwrite() on conflicts */

        cmd_resolve_path(&eff, pa.pos[0], rfull, sizeof(rfull));
        if (pa.npos > 1) {
            strncpy(lbuf, pa.pos[1], sizeof(lbuf) - 1); lbuf[sizeof(lbuf) - 1] = '\0';
        } else {
            const char *slash = strrchr(rfull, '/');
            strncpy(lbuf, slash ? slash + 1 : rfull, sizeof(lbuf) - 1);
            lbuf[sizeof(lbuf) - 1] = '\0';
        }
        copyctx_init(&c, !pa.force);
        count_remote_tree(&eff, rfull, &c.pr);
        panel_get_dir(&eff, rfull, lbuf, &c);
        if (c.cancelled) printf("Abgebrochen.\n");
        printf("%d Datei(en) ok, %d Fehler\n", c.pr.ok, c.pr.err);
    } else {
        cmd_get_checked(&eff, pa.pos[0], pa.npos > 1 ? pa.pos[1] : NULL, pa.force);
    }
}

/* Wildcard-Zweig von `put`: spiegelbildlich zu do_get_pattern(), listet
 * das lokale Verzeichnis dirpart statt einer Remote-Anfrage. */
static void do_put_pattern(const AcConfig *eff,
                          const char *dirpart, const char *pat,
                          const char *remote_arg, int recursive, int force)
{
    const char *ldir = (dirpart && *dirpart) ? dirpart : ".";
    char        rdestdir[AC_MAX_PATH];
    LocalDir   *d;
    LocalDirEntry e;
    Pattern    *p;
    CopyCtx     c; /* ask = !force: prompts via ui_overwrite() on conflicts */
    int         matches = 0;
    AcEntry    *dst_entries = NULL;
    int         dst_n = 0;

    if (remote_arg && *remote_arg) {
        cmd_resolve_path(eff, remote_arg, rdestdir, sizeof(rdestdir));
    } else {
        strncpy(rdestdir, eff->basepath, sizeof(rdestdir) - 1);
        rdestdir[sizeof(rdestdir) - 1] = '\0';
    }

    p = pattern_compile(pat);
    if (!p) { fprintf(stderr, "amicloud: put: ungueltiges Muster '%s'\n", pat); return; }

    /* Pass 1: count matches (+ recurse-count matched directories) — a
     * second local opendir()/readdir() pass is effectively free, unlike
     * the remote case (plan §17.1). */
    copyctx_init(&c, !force);
    d = localfs_opendir(ldir);
    if (!d) {
        fprintf(stderr, "amicloud: put: kann %s nicht oeffnen\n", ldir);
        pattern_free(p);
        return;
    }
    while (localfs_readdir(d, &e)) {
        if (!pattern_match(p, e.name)) continue;
        matches++;
        if (e.is_dir) {
            if (recursive) {
                char lchild[AC_MAX_PATH];
                localfs_join(lchild, sizeof(lchild), ldir, e.name);
                count_local_tree(lchild, &c.pr);
            }
        } else {
            c.pr.total++;
            c.pr.bytes_total += e.size;
        }
    }
    localfs_closedir(d);

    /* One listing of the destination instead of a request per file:
     * existence + size/date/MD5 for the overwrite prompt. cmd_is_dir()
     * first, silently -- a destination that does not exist yet is the
     * normal case, not an error (same as panel_put_dir()). */
    if (c.ask && cmd_is_dir(eff, rdestdir))
        cmd_list_raw(eff, rdestdir, &dst_entries, &dst_n);

    cmd_mkdir_quiet(eff, rdestdir);

    /* Pass 2: transfer. */
    matches = 0;
    d = localfs_opendir(ldir);
    if (!d) { pattern_free(p); return; } /* directory vanished mid-run — unlikely */
    while (localfs_readdir(d, &e)) {
        char lchild[AC_MAX_PATH], rchild[AC_MAX_PATH];
        if (!pattern_match(p, e.name)) continue;
        matches++;
        localfs_join(lchild, sizeof(lchild), ldir, e.name);
        snprintf(rchild, sizeof(rchild), "%s/%s", rdestdir, e.name);
        if (e.is_dir) {
            if (recursive) panel_put_dir(eff, lchild, rchild, &c);
            else fprintf(stderr,
                    "amicloud: put: '%s' ist ein Verzeichnis (-r verwenden)\n",
                    e.name);
        } else {
            FileMeta smeta, dmeta;
            const AcEntry *ae = find_remote_file(dst_entries, dst_n, e.name);
            smeta.size = e.size;
            strncpy(smeta.date, e.modified, sizeof(smeta.date) - 1);
            smeta.date[sizeof(smeta.date) - 1] = '\0';
            smeta.md5[0] = '\0';
            if (ae) meta_from_entry(&dmeta, ae);
            panel_copy_file(PANEL_LOCAL, NULL, lchild, PANEL_CLOUD, eff, rchild,
                            e.name, &smeta, ae ? &dmeta : NULL, &c);
        }
        if (c.cancelled) { printf("Abgebrochen.\n"); break; }
    }
    localfs_closedir(d);
    cmd_entries_free(dst_entries);
    pattern_free(p);

    if (matches == 0) printf("Keine Treffer fuer '%s'\n", pat);
    else               printf("%d Datei(en) ok, %d Fehler\n", c.pr.ok, c.pr.err);
}

static void do_put(ShellSession *s, char **tok, int n)
{
    ParsedArgs pa;
    AcConfig eff;
    char dirpart[AC_MAX_PATH];
    const char *pat;
    parse_args(tok, n, &pa);

    if (pa.npos < 1) { fprintf(stderr, "amicloud: put requires <local> [remote]\n"); return; }
    eff_config(s, &eff);

    pat = split_pattern(pa.pos[0], 1, dirpart, sizeof(dirpart));
    if (pat) {
        do_put_pattern(&eff, dirpart, pat,
                       pa.npos > 1 ? pa.pos[1] : NULL, pa.recursive, pa.force);
        return;
    }

    if (pa.recursive) {
        char rbuf[AC_MAX_PATH];
        CopyCtx c; /* ask = !force: prompts via ui_overwrite() on conflicts */

        if (pa.npos > 1) {
            cmd_resolve_path(&eff, pa.pos[1], rbuf, sizeof(rbuf));
        } else {
            const char *sep  = last_sep(pa.pos[0], 1);
            const char *name = sep ? sep + 1 : pa.pos[0];
            if (!*name) {
                fprintf(stderr, "amicloud: put -r: '%s' nennt keinen Ordnernamen -- "
                        "Ziel angeben (put -r <lokal> <remote>)\n", pa.pos[0]);
                return;
            }
            cmd_resolve_path(&eff, name, rbuf, sizeof(rbuf));
        }
        copyctx_init(&c, !pa.force);
        count_local_tree(pa.pos[0], &c.pr);
        panel_put_dir(&eff, pa.pos[0], rbuf, &c);
        if (c.cancelled) printf("Abgebrochen.\n");
        printf("%d Datei(en) ok, %d Fehler\n", c.pr.ok, c.pr.err);
    } else {
        cmd_put_checked(&eff, pa.pos[0], pa.npos > 1 ? pa.pos[1] : NULL, pa.force);
    }
}

static void do_del(ShellSession *s, char **tok, int n)
{
    ParsedArgs pa;
    AcConfig eff;
    parse_args(tok, n, &pa);

    if (pa.npos < 1) { fprintf(stderr, "amicloud: del requires <path>\n"); return; }
    eff_config(s, &eff);

    if (pa.recursive) {
        char rfull[AC_MAX_PATH];
        cmd_resolve_path(&eff, pa.pos[0], rfull, sizeof(rfull));
        if (!pa.force) {
            char msg[AC_MAX_PATH + 64];
            snprintf(msg, sizeof(msg), "Delete '%s' and ALL contents?", rfull);
            if (!ui_confirm(msg)) { printf("Abgebrochen.\n"); return; }
        }
        cmd_del_recursive(&eff, pa.pos[0]);
    } else {
        cmd_del(&eff, pa.pos[0]);
    }
}

static void do_mkdir(ShellSession *s, const char *arg)
{
    AcConfig eff;
    if (!arg) { fprintf(stderr, "amicloud: mkdir requires <path>\n"); return; }
    eff_config(s, &eff);
    cmd_mkdir(&eff, arg);
}

static void do_rename(ShellSession *s, const char *from, const char *to)
{
    AcConfig eff;
    if (!from || !to) { fprintf(stderr, "amicloud: rename requires <old> <new>\n"); return; }
    eff_config(s, &eff);
    cmd_rename(&eff, from, to);
}

static void do_info(ShellSession *s, const char *arg)
{
    AcConfig eff;
    if (!arg) { fprintf(stderr, "amicloud: info requires <path>\n"); return; }
    eff_config(s, &eff);
    cmd_info(&eff, arg);
}

/* md5 <remote> / lmd5 <lokal> — Ausgabeformat wie md5sum, damit man Werte
 * direkt nebeneinanderlegen kann (plan §18.5). Bewusst kein Wildcard-
 * Support: fuer "viele Dateien" ist ls -m/lls -m der richtige Weg. */
static void do_md5(ShellSession *s, const char *arg)
{
    AcConfig eff;
    char hex[33];
    if (!arg) { fprintf(stderr, "amicloud: md5 requires <remote>\n"); return; }
    eff_config(s, &eff);
    if (cmd_get_md5(&eff, arg, hex, sizeof(hex)) == 0)
        printf("%s  %s\n", hex, arg);
}

/* search/find <muster> [scope] — Standard-Scope ist der aktuelle
 * Remote-cwd (rekursiv), ueber eff_config() wie jeder andere Befehl;
 * "scope" (optional) ueberschreibt das (z.B. "/" fuer die ganze Cloud). */
static void do_search(ShellSession *s, char **tok, int n)
{
    AcConfig eff;
    AcSearchHit *hits;
    int i, count, truncated;
    const char *pattern = (n > 0) ? tok[0] : NULL;
    const char *scope   = (n > 1) ? tok[1] : NULL;

    if (!pattern) { fprintf(stderr, "amicloud: search requires <pattern>\n"); return; }

    eff_config(s, &eff);
    if (!cmd_search(&eff, scope, pattern, 1, &hits, &count, &truncated)) return;

    for (i = 0; i < count; i++) {
        printf("%s%s\n", hits[i].path, hits[i].is_dir ? "/" : "");
    }
    if (truncated) {
        fprintf(stderr, "amicloud: search: Ergebnis gekappt (Server-Limit erreicht)\n");
    }
    cmd_search_free(hits);
}

static void do_lmd5(const char *arg)
{
    char hex[33];
    if (!arg) { fprintf(stderr, "amicloud: lmd5 requires <lokal>\n"); return; }
    if (md5_file_hex(arg, hex))
        printf("%s  %s\n", hex, arg);
    else
        fprintf(stderr, "amicloud: lmd5: cannot open '%s'\n", arg);
}

/* ------------------------------------------------------------------ */
/* Dispatch — returns 0 to stop the shell, 1 to continue.                */
/* ------------------------------------------------------------------ */
static int dispatch(ShellSession *s, char **tok, int n)
{
    const char *cmd = tok[0];

    if (ac_strcasecmp(cmd, "quit") == 0 || ac_strcasecmp(cmd, "exit") == 0 ||
        ac_strcasecmp(cmd, "bye") == 0)
        return 0;

    if (ac_strcasecmp(cmd, "help") == 0) { print_help(); return 1; }
    if (ac_strcasecmp(cmd, "pwd") == 0) { printf("%s\n", s->remote_cwd); return 1; }
    if (ac_strcasecmp(cmd, "lpwd") == 0) {
        char b[AC_MAX_PATH];
        if (localfs_getcwd(b, sizeof(b))) printf("%s\n", b);
        else fprintf(stderr, "amicloud: cannot determine local directory\n");
        return 1;
    }
    if (ac_strcasecmp(cmd, "cd") == 0)   { do_cd(s, n > 1 ? tok[1] : NULL); return 1; }
    if (ac_strcasecmp(cmd, "lcd") == 0)  { do_lcd(s, n > 1 ? tok[1] : NULL); return 1; }
    if (ac_strcasecmp(cmd, "ls") == 0 || ac_strcasecmp(cmd, "dir") == 0 ||
        ac_strcasecmp(cmd, "list") == 0)
        { do_ls(s, tok + 1, n - 1); return 1; }
    if (ac_strcasecmp(cmd, "lls") == 0 || ac_strcasecmp(cmd, "ldir") == 0 ||
        ac_strcasecmp(cmd, "llist") == 0)
        { do_lls(tok + 1, n - 1); return 1; }
    if (ac_strcasecmp(cmd, "get") == 0)  { do_get(s, tok + 1, n - 1); return 1; }
    if (ac_strcasecmp(cmd, "put") == 0)  { do_put(s, tok + 1, n - 1); return 1; }
    if (ac_strcasecmp(cmd, "del") == 0 || ac_strcasecmp(cmd, "rm") == 0 ||
        ac_strcasecmp(cmd, "delete") == 0)
        { do_del(s, tok + 1, n - 1); return 1; }
    if (ac_strcasecmp(cmd, "mkdir") == 0 || ac_strcasecmp(cmd, "makedir") == 0)
        { do_mkdir(s, n > 1 ? tok[1] : NULL); return 1; }
    if (ac_strcasecmp(cmd, "rename") == 0 || ac_strcasecmp(cmd, "mv") == 0)
        { do_rename(s, n > 1 ? tok[1] : NULL, n > 2 ? tok[2] : NULL); return 1; }
    if (ac_strcasecmp(cmd, "info") == 0 || ac_strcasecmp(cmd, "stat") == 0)
        { do_info(s, n > 1 ? tok[1] : NULL); return 1; }
    if (ac_strcasecmp(cmd, "search") == 0 || ac_strcasecmp(cmd, "find") == 0)
        { do_search(s, tok + 1, n - 1); return 1; }
    if (ac_strcasecmp(cmd, "md5") == 0)  { do_md5(s, n > 1 ? tok[1] : NULL); return 1; }
    if (ac_strcasecmp(cmd, "lmd5") == 0) { do_lmd5(n > 1 ? tok[1] : NULL); return 1; }
    if (ac_strcasecmp(cmd, "history") == 0) {
        if (n > 1 && ac_strcasecmp(tok[1], "clear") == 0) {
            history_clear();
            printf("History geloescht.\n");
        } else {
            int i, hc = history_count();
            for (i = 0; i < hc; i++)
                printf("%3d  %s\n", i + 1, history_at(i));
        }
        return 1;
    }

    fprintf(stderr, "amicloud: unknown command '%s' (type 'help')\n", cmd);
    return 1;
}

/* Line reader for prompts while commands are read from a file or pipe:
 * no answer is possible there, so the prompt takes its safe choice
 * (confirm: no, overwrite: cancel) -- see ui_set_line_reader(). */
static int batch_no_answer(const char *prompt, char *buf, size_t bufsz)
{
    (void)buf; (void)bufsz;
    printf("%s\n(keine Antwort moeglich, Befehle kommen aus einer Datei -- "
           "-f/FORCE verwenden)\n", prompt);
    return -1;
}

/* ------------------------------------------------------------------ */
int shell_run(const AcConfig *cfg)
{
    ShellSession sess;
    char line[1024];
    char *tokens[MAX_TOKENS];
    int running = 1;
    int interactive;

    memset(&sess, 0, sizeof(sess));
    sess.base_cfg = *cfg;
    strncpy(sess.remote_cwd, cfg->basepath, sizeof(sess.remote_cwd) - 1);
    if (!localfs_getcwd(sess.local_start_cwd, sizeof(sess.local_start_cwd)))
        sess.local_start_cwd[0] = '\0';
    localfs_set_tz(cfg->gmt_offset, cfg->dst);

    print_banner(&sess);

    history_reset();
    /* Faellt auf den bisherigen fgets()-Pfad zurueck, wenn stdin kein
     * echtes Terminal ist (Pipe/Redirect/Heredoc) — Rohmodus + eigene
     * History-Navigation ergeben dort keinen Sinn. */
    interactive = lineedit_init();
    /* Answers to the overwrite/confirm prompts must come through the same
     * input path as the commands: interactively via the line editor (the
     * terminal is in raw mode for the whole session, so the stdio default
     * would neither echo the answer nor, on the Amiga, see a line end);
     * when commands come from a file or pipe, never read an answer at all
     * -- it would swallow the next command line. */
    ui_set_line_reader(interactive ? lineedit_readline : batch_no_answer);

    while (running) {
        int ntok;
        size_t len;

        if (interactive) {
            char prompt[AC_MAX_PATH + 8];
            int  rl;
            build_prompt(&sess, prompt, sizeof(prompt));
            rl = lineedit_readline(prompt, line, sizeof(line));
            if (rl < 0) { putchar('\n'); break; }
            len = (size_t)rl;
        } else {
            print_prompt(&sess);
            if (!fgets(line, sizeof(line), stdin)) { printf("\n"); break; }
            len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
        }

        if (len > 0) history_add(line);

        ntok = tokenize(line, tokens, MAX_TOKENS);
        if (ntok == 0) continue;

        running = dispatch(&sess, tokens, ntok);
    }

    ui_set_line_reader(NULL);
    if (interactive) lineedit_shutdown();
    return 0;
}
