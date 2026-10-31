/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
/*
 * gui.c — AmiCloud Commander main window + event loop (Amiga/MUI).
 * UNVERIFIED — see the notice in gui.h. Modelled closely on the
 * reference examples in mui38dev/MUI/Developer/C/Examples/ (EnvBrowser.c
 * for the overall Application/Window/MUIM_Application_Input shape,
 * WbMan.c for the string-input sub-window and Hook-based button
 * callback).
 *
 * v1 scope per the plan (section 7.3): synchronous operations with a
 * busy pointer, cancel handled by polling MUIM_Application_NewInput
 * between chunks inside the ui_progress hook — no worker Task/process.
 * Multi-select (shift-click, MUIA_Listview_MultiSelect_Shifted — see
 * panel_mui.c) is supported for F5 Copy/Pruefsumme via
 * MUIM_List_NextSelected; other operations (F6/F7/F8) stay single-entry.
 */
#include <libraries/mui.h>
#include <libraries/iffparse.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>

#include "gui.h"
#include "cmd.h"
#include "charset.h"
#include "localfs.h"
#include "ui_hooks.h"
#include "mui_stubs.h"
#include "md5.h"
#include "search.h"
#include "cloudtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* IDs dispatched by the main MUIM_Application_Input loop                */
/* ------------------------------------------------------------------ */
#define ID_QUIT          1
#define ID_RELOAD        2
#define ID_INFO          3
#define ID_COPY          4
#define ID_MOVE          5
#define ID_MKDIR         6
#define ID_DELETE        7
#define ID_ABOUT         8
#define ID_SEARCH        9

/* Menu-only actions (no toolbar button of their own) — own block, the
 * 8-9 gap before ID_LEFT_* isn't big enough for both. */
#define ID_CHECKSUM     41
#define ID_RENAME       42
#define ID_SEARCH_GOTO  43

/* Cap on Commander-side search results (docs/design/search-plan.md
 * section 5, MUI part) -- both the cloud and local branches of do_search()
 * copy into an array of this size. Independent of the server's own 2000-
 * row LIMIT (action=search) -- a 68k Amiga with limited RAM has a much
 * tighter budget per SearchHit (~1 KB, dominated by path[1024]) than a PC
 * build, so this stays deliberately small. */
#define SEARCH_MAX_HITS 500

#define ID_LEFT_SOURCE  10
#define ID_LEFT_GO      11
#define ID_LEFT_DCLICK  12
#define ID_LEFT_UP      13
#define ID_LEFT_ACTIVE  14
#define ID_LEFT_SELECT  15
#define ID_RIGHT_SOURCE 20
#define ID_RIGHT_GO     21
#define ID_RIGHT_DCLICK 22
#define ID_RIGHT_UP     23
#define ID_RIGHT_ACTIVE 24
#define ID_RIGHT_SELECT 25

#define ID_INPUT_OK    900
#define ID_INPUT_CANCEL 901
#define ID_CANCEL_XFER 902

static Object *g_app;
static Object *g_main_status;
static Object *g_busy_text;
static AmigaPanel g_left, g_right;
static AmigaPanel *g_active = &g_left;

static Object *g_input_window, *g_input_string, *g_input_ok, *g_input_cancel;
static Object *g_progress_window, *g_progress_text, *g_progress_gauge, *g_cancel_button;
static int     g_cancel_requested;

/* Search results window (docs/design/search-plan.md section 5, MUI
 * part) — global single instance, built once in gui_run() and reused
 * across searches (MUIM_List_Clear + refill instead of a second window),
 * mirroring g_input_window/g_progress_window above. g_search_hits is a
 * fixed SEARCH_MAX_HITS-sized buffer, reused the same way. */
static Object    *g_search_window, *g_search_list, *g_search_listview;
static SearchHit *g_search_hits;
static int        g_search_n;

/* bps display for the progress window's text line — shared by every
 * ui_progress() caller (upload/download, both directions) via the same
 * mui_progress() hook. Reset whenever a new file starts (done==0, see
 * panel_copy_file()'s up-front ui_progress(0, size, name) ping) so a run
 * of several small files doesn't show one file's leftover rate on the
 * next. g_bps_text is only refreshed on ui_rate_sample()'s throttled
 * tick (~1/sec), not every chunk. */
static UiRateMeter g_rate;
static char        g_bps_text[32] = "";

/* Set for the duration of a CopyCtx-driven operation (do_copy()) so
 * mui_progress() can read the overall x/y file counter (CopyCtx.pr) off
 * it — ui_progress()'s own done/total are per-file bytes, not the outer
 * counter, see the mui_progress() comment below. NULL outside do_copy()
 * (mkdir/rename/delete/move show only the per-file text, no gauge). */
static CopyCtx *g_op;

/* ------------------------------------------------------------------ */
/* Busy indicator (gui.h) — see gui.h for why this exists alongside      */
/* begin_operation()/end_operation() below.                              */
/* ------------------------------------------------------------------ */
static int g_busy_depth;
static Object *g_main_window;

/* Only the main window sleeps (busy pointer, no input), never the whole
 * application: MUIA_Application_Sleep also froze the progress window, so
 * its "Abbrechen" button could not be pressed during a transfer (found
 * running it in Amiberry). */
void gui_busy_begin(void)
{
    if (g_busy_depth++ == 0) {
        if (g_main_window) set(g_main_window, MUIA_Window_Sleep, TRUE);
        if (g_busy_text) set(g_busy_text, MUIA_Text_Contents, "Online-Zugriff...");
    }
}

void gui_busy_end(void)
{
    if (g_busy_depth > 0 && --g_busy_depth == 0) {
        if (g_main_window) set(g_main_window, MUIA_Window_Sleep, FALSE);
        if (g_busy_text) set(g_busy_text, MUIA_Text_Contents, "");
    }
}

/* ------------------------------------------------------------------ */
/* ui_hooks bridge — one active operation at a time, so plain statics    */
/* (no per-call userdata in the ui_hooks.h function-pointer interface)   */
/* are enough, same reasoning as the Qt Commander's g_activeJob         */
/* (apps/amicloud-commander-qt/src/transferjob.cpp).                     */
/* ------------------------------------------------------------------ */
static void mui_notify(int level, const char *msg)
{
    (void)level;
    if (g_main_status) set(g_main_status, MUIA_Text_Contents, msg);
}

static int mui_confirm(const char *msg)
{
    return MUI_Request(g_app, NULL, 0, "Bestaetigen", "_Ja|_Nein", (char *)msg) == 1;
}

/* done/total here are per-file bytes (see ui_hooks.h) — the current
 * file's own progress, called from deep inside cmd_get()/cmd_put()
 * (http_amiga.c) or once up front by panel_copy_file() (done=0). The
 * *overall* x/y file counter across the whole copy comes from g_op->pr
 * instead — g_op is only set while do_copy() is driving a CopyCtx (see
 * its declaration above); every other begin_operation() user (mkdir,
 * rename, delete, move) leaves it NULL and gets just the per-file line,
 * same as before this rework. */
static int mui_progress(long done, long total, const char *name)
{
    char buf[160];
    ULONG sigs = 0;

    if (done == 0) { ui_rate_reset(&g_rate); g_bps_text[0] = '\0'; }
    if (ui_rate_sample(&g_rate, done)) {
        char rate[24];
        ui_format_rate(rate, sizeof(rate), g_rate.bps);
        snprintf(g_bps_text, sizeof(g_bps_text), "  %s", rate);
    }

    /* name is a full path (up to AC_MAX_PATH bytes) — snprintf, not sprintf,
     * since it routinely exceeds this display buffer (was a stack overflow
     * that crashed on any long download/upload path). */
    if (total > 0)
        snprintf(buf, sizeof(buf), "%s (%ld%%)%s", name ? name : "",
                 total > 2000000L ? done / (total / 100) : (done * 100) / total, g_bps_text);
    else
        snprintf(buf, sizeof(buf), "%s%s", name ? name : "...", g_bps_text);
    if (g_progress_text) set(g_progress_text, MUIA_Text_Contents, buf);

    if (g_progress_gauge) {
        if (g_op && g_op->pr.bytes_total > 0) {
            /* Exact overall byte position across the whole multi-file
             * operation: bytes_done was pre-incremented by the full
             * current-file size in progress_prefix() (panel.c), so
             * subtracting cur_file_size back out and adding the live
             * done reconstructs "bytes completed by every prior file,
             * plus how far the in-flight file actually got" — smooth
             * movement within a file instead of a jump per file. */
            char info[32];
            long overall = g_op->pr.bytes_done - g_op->pr.cur_file_size + done;
            if (overall < 0) overall = 0;
            if (overall > g_op->pr.bytes_total) overall = g_op->pr.bytes_total;
            snprintf(info, sizeof(info), "%d/%d", g_op->pr.done, g_op->pr.total);
            set(g_progress_gauge, MUIA_Gauge_InfoText, info);
            set(g_progress_gauge, MUIA_Gauge_Current, (LONG)((overall * 100) / g_op->pr.bytes_total));
        } else {
            set(g_progress_gauge, MUIA_Gauge_InfoText, "");
            set(g_progress_gauge, MUIA_Gauge_Current, 0);
        }
    }

    /* Pump pending Intuition/MUI events (Cancel button, window redraws)
     * without a worker Task — plan section 7.3. */
    DoMethod(g_app, MUIM_Application_NewInput, &sigs);

    return g_cancel_requested ? 0 : 1;
}

/* Per-file/per-folder overwrite prompt — ui_hooks.h's UiHooks.overwrite,
 * installed below into g_mui_hooks. Called synchronously from inside
 * panel_copy_file()/panel_get_dir()/panel_put_dir() while do_copy() is
 * mid-transfer, i.e. while the main window is already asleep (see
 * begin_operation()) — back it off around the nested MUI_Request just
 * like the old per-file cmd_exists() pre-check did (same untested-nesting
 * concern noted where gui_busy_begin()/_end() is declared). */
static UiOverwrite mui_overwrite(int is_dir, const char *name,
                                 long src_size, const char *src_date, const char *src_md5,
                                 long dst_size, const char *dst_date, const char *dst_md5)
{
    char msg[512];
    LONG result;

    if (is_dir) {
        snprintf(msg, sizeof(msg),
                "Verzeichnis \"%s\" existiert bereits.\nInhalt zusammenfuehren?", name);
    } else {
        snprintf(msg, sizeof(msg),
                "\"%s\" existiert bereits.\n\n"
                "Neu:       %ld Bytes, %s%s%s\n"
                "Vorhanden: %ld Bytes, %s%s%s",
                name,
                src_size, src_date ? src_date : "",
                (src_md5 && src_md5[0]) ? ", MD5 " : "", (src_md5 && src_md5[0]) ? src_md5 : "",
                dst_size, dst_date ? dst_date : "",
                (dst_md5 && dst_md5[0]) ? ", MD5 " : "", (dst_md5 && dst_md5[0]) ? dst_md5 : "");
    }

    gui_busy_end();
    result = MUI_Request(g_app, NULL, 0, "Ueberschreiben?",
                         "_Ueberschreiben|Ueberspri_ngen|Alle ueberschreib_en|Alle ue_berspringen|_Abbruch",
                         msg);
    gui_busy_begin();

    switch (result) {
    case 1: return UI_OW_OVERWRITE;
    case 2: return UI_OW_SKIP;
    case 3: return UI_OW_OVERWRITE_ALL;
    case 4: return UI_OW_SKIP_ALL;
    default: return UI_OW_CANCEL; /* 5 = Abbruch, 0 = window closed/ESC */
    }
}

static UiHooks g_mui_hooks = { mui_notify, mui_confirm, mui_progress, mui_overwrite };

static __saveds LONG cancel_hook_func(__reg("a0") struct Hook *hook,
                                       __reg("a2") Object *obj,
                                       __reg("a1") APTR msg)
{
    (void)hook; (void)obj; (void)msg;
    g_cancel_requested = 1;
    return 0;
}
static struct Hook cancel_hook = { { NULL, NULL }, (APTR)cancel_hook_func, NULL, NULL };

/* g_mui_hooks is installed once in gui_run(), for the whole process
 * lifetime — not just around begin_operation()/end_operation() — so that
 * nothing ever falls back to ui_hooks.c's default console hooks (plain
 * printf()/fprintf(stderr,...)), which is what used to pop open an
 * unwanted output/console window under vbcc the moment any ui_notify()
 * fired outside a begin_operation()/end_operation() bracket. */
static void begin_operation(void)
{
    g_cancel_requested = 0;
    ui_rate_reset(&g_rate);
    g_bps_text[0] = '\0';
    set(g_progress_text, MUIA_Text_Contents, "");
    if (g_progress_gauge) {
        set(g_progress_gauge, MUIA_Gauge_InfoText, "");
        set(g_progress_gauge, MUIA_Gauge_Current, 0);
    }
    set(g_progress_window, MUIA_Window_Open, TRUE);
    gui_busy_begin();
}

static void end_operation(void)
{
    /* A cancel ends with its operation: left set, mui_progress() would
     * abort every later request too -- even refreshing a panel failed
     * with "network error" after one press of "Abbrechen". */
    g_cancel_requested = 0;
    gui_busy_end();
    set(g_progress_window, MUIA_Window_Open, FALSE);
}

/* ------------------------------------------------------------------ */
/* Small modal-ish string input window (WbMan.c's string-requester       */
/* pattern) — used by "Umbenennen" (RAmiga+R) and F7 (mkdir).            */
/* ------------------------------------------------------------------ */
static void build_input_dialog(void)
{
    g_input_string = StringObject, StringFrame, MUIA_String_MaxLen, 200, End;
    g_input_ok     = SimpleButton("_OK");
    g_input_cancel = SimpleButton("_Abbrechen");

    g_input_window = WindowObject,
        MUIA_Window_Title, "Eingabe",
        MUIA_Window_Menu,  MUIV_Window_Menu_NoMenu,
        WindowContents, VGroup,
            Child, g_input_string,
            Child, HGroup,
                Child, g_input_ok,
                Child, g_input_cancel,
            End,
        End,
    End;

    DoMethod(g_app, OM_ADDMEMBER, g_input_window);
    DoMethod(g_input_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_INPUT_CANCEL);
    DoMethod(g_input_ok, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_INPUT_OK);
    DoMethod(g_input_cancel, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_INPUT_CANCEL);
    DoMethod(g_input_string, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
            g_app, 2, MUIM_Application_ReturnID, ID_INPUT_OK);
}

/* Returns 1 + *out filled if OK/Return, 0 if cancelled/closed. */
static int ask_string(const char *title, const char *initial, char *out, size_t outsz)
{
    ULONG sigs = 0;
    LONG  id;

    set(g_input_window, MUIA_Window_Title, title);
    set(g_input_string, MUIA_String_Contents, initial ? initial : "");
    set(g_input_window, MUIA_Window_Open, TRUE);
    set(g_input_window, MUIA_Window_ActiveObject, g_input_string);

    for (;;) {
        id = DoMethod(g_app, MUIM_Application_NewInput, &sigs);
        if (id == ID_INPUT_OK) {
            STRPTR text = NULL;
            get(g_input_string, MUIA_String_Contents, &text);
            if (text) { strncpy(out, (char *)text, outsz - 1); out[outsz - 1] = '\0'; }
            else out[0] = '\0';
            set(g_input_window, MUIA_Window_Open, FALSE);
            return 1;
        }
        if (id == ID_INPUT_CANCEL) {
            set(g_input_window, MUIA_Window_Open, FALSE);
            return 0;
        }
        if (sigs) Wait(sigs);
    }
}

/* ------------------------------------------------------------------ */
static AmigaPanel *other_panel(void) { return g_active == &g_left ? &g_right : &g_left; }

static void set_active(AmigaPanel *p)
{
    g_active = p;
    /* Persistent visual marker on the active panel itself, not just the
     * easy-to-miss status line below: MUIA_Background is one of the few
     * Group attributes still settable after realization (MUIA_Frame is
     * init-only, "i.." in the SDK docs) — MUII_SelectedBack is MUI's own
     * "this is selected" image, MUII_GroupBack the plain default look. */
    set(g_left.group,  MUIA_Background, (p == &g_left)  ? MUII_SelectedBack : MUII_GroupBack);
    set(g_right.group, MUIA_Background, (p == &g_right) ? MUII_SelectedBack : MUII_GroupBack);
    mui_notify(UI_INFO, p == &g_left ? "Aktiv: links" : "Aktiv: rechts");
}

/* ------------------------------------------------------------------ */
/* Operations — mirrors the Qt Commander's TransferJob::runOne(),       */
/* single-entry only (see file header).                                  */
/* ------------------------------------------------------------------ */
static void do_reload(void) { panel_mui_reload(g_active); }

static void do_info(void)
{
    PanelEntry e;
    char path[AC_MAX_PATH];
    char msg[512];

    if (!panel_mui_current_entry(g_active, &e)) return;
    panel_mui_child_path(g_active, &e, path, sizeof(path));

    if (g_active->source == PANEL_CLOUD) {
        AcInfo info;
        int ok;

        gui_busy_begin();
        ok = cmd_info_raw(&g_active->cfg, path, &info);
        gui_busy_end();

        if (ok != 0) {
            MUI_Request(g_app, NULL, 0, "Info", "_OK", "Konnte Info nicht laden.");
            return;
        }
        {
            char lname[256];
            cs_from_utf8(info.name, lname, sizeof(lname));   /* Phase 4 */
            sprintf(msg, "Name: %s\nTyp: %s\nGroesse: %ld Bytes\nGeaendert: %s\nMD5: %s",
                   lname, info.type, info.size, info.modified, info.md5);
        }
    } else {
        long size = 0, mtime = 0;
        char hex[33] = "";

        localfs_stat(path, &size, &mtime);
        /* Local entries carry no MD5 from the panel listing (only cloud
         * ones do) -- computed on demand here, same helper/pattern as
         * do_checksum() below and panel.c's overwrite-confirm dialog. */
        if (!e.is_dir) {
            gui_busy_begin();
            md5_file_hex(path, hex);
            gui_busy_end();
        }
        sprintf(msg, "Name: %s\nGroesse: %ld Bytes\nGeaendert: %s\nMD5: %s",
               e.name, size, e.modified, hex);
    }
    MUI_Request(g_app, NULL, 0, "Info", "_OK", msg);
}

/* dst's directory listing is already loaded in memory (the panel we're
 * looking at) — looking a name up in it is how the top-level overwrite
 * check avoids the old per-file cmd_exists() HTTP round-trip. Recursive
 * subfolder copies get the same in-memory treatment one level down inside
 * panel_get_dir()/panel_put_dir() (panel.c), via cmd_list_raw()/a local
 * directory snapshot instead of a per-file existence probe. */
static int find_dst_meta(const AmigaPanel *p, const char *name, FileMeta *out)
{
    int i;
    for (i = 0; i < p->list.n; i++) {
        const PanelEntry *pe = &p->list.items[i];
        if (!pe->is_dir && strcmp(pe->name, name) == 0) {
            out->size = pe->size;
            strncpy(out->date, pe->modified, sizeof(out->date) - 1);
            out->date[sizeof(out->date) - 1] = '\0';
            strncpy(out->md5, pe->md5, sizeof(out->md5) - 1);
            out->md5[sizeof(out->md5) - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

/* Copies every entry the user multi-selected in src (shift/ctrl-click
 * range or individual marks — see panel_mui_next_selected()), or just the
 * cursor entry when nothing is explicitly marked, in one F5 press.
 *
 * Single pass now, no separate "N Objekte kopieren?" confirmation (that
 * only makes sense once, up front, for Verschieben — see
 * do_move()) and no per-file existence pre-check ahead of it (that
 * per-file cmd_exists() round-trip, serialized before the first byte ever
 * moved, was the actual source of the long "hourglass, then it asks
 * again" wait). Existence and the overwrite decision are handled inline,
 * per conflict, as CopyCtx/panel_copy_file() find them — see panel.h. A
 * quick byte/file-count scan up front (no network beyond what listing
 * already did for uploads; the same `list` round-trips downloads need
 * anyway) feeds the progress gauge's x/y from the very first file. */
static void do_copy(void)
{
    AmigaPanel *src = g_active;
    AmigaPanel *dst = other_panel();
    LONG    cookie;
    PanelEntry e;
    CopyCtx ctx;
    char    result_msg[64];

    if (src->source == PANEL_CLOUD && dst->source == PANEL_CLOUD) {
        MUI_Request(g_app, NULL, 0, "Kopieren", "_OK",
                   "AmiCloud->AmiCloud-Kopieren wird noch nicht unterstuetzt.");
        return;
    }

    cookie = MUIV_List_NextSelected_Start;
    if (!panel_mui_next_selected(src, &cookie, &e)) return; /* nothing to copy */

    copyctx_init(&ctx, 1);
    begin_operation();
    g_op = &ctx;

    set(g_progress_text, MUIA_Text_Contents, "Ermittle Dateien...");
    cookie = MUIV_List_NextSelected_Start;
    while (panel_mui_next_selected(src, &cookie, &e)) {
        if (e.is_dir) {
            char srcPath[AC_MAX_PATH];
            panel_mui_child_path(src, &e, srcPath, sizeof(srcPath));
            if (src->source == PANEL_LOCAL) count_local_tree(srcPath, &ctx.pr);
            else                            count_remote_tree(&src->cfg, srcPath, &ctx.pr);
        } else {
            ctx.pr.total++;
            ctx.pr.bytes_total += e.size;
        }
    }

    cookie = MUIV_List_NextSelected_Start;
    while (panel_mui_next_selected(src, &cookie, &e)) {
        char srcPath[AC_MAX_PATH], dstPath[AC_MAX_PATH];

        if (ctx.cancelled || g_cancel_requested) break;

        panel_mui_child_path(src, &e, srcPath, sizeof(srcPath));
        panel_mui_child_path(dst, &e, dstPath, sizeof(dstPath));

        if (e.is_dir) {
            if (src->source == PANEL_LOCAL && dst->source == PANEL_CLOUD)
                panel_put_dir(&dst->cfg, srcPath, dstPath, &ctx);
            else if (src->source == PANEL_CLOUD && dst->source == PANEL_LOCAL)
                panel_get_dir(&src->cfg, srcPath, dstPath, &ctx);
            /* Local->Local dir: falls through to the single mui_notify()
             * warning below, same v1-scope carve-out as the file case. */
            else
                mui_notify(UI_WARN, "Local->Local-Kopieren wird auf dem Amiga (noch) nicht angeboten.");
        } else {
            FileMeta smeta, dmeta;
            int dst_exists = find_dst_meta(dst, e.name, &dmeta);

            smeta.size = e.size;
            strncpy(smeta.date, e.modified, sizeof(smeta.date) - 1);
            smeta.date[sizeof(smeta.date) - 1] = '\0';
            strncpy(smeta.md5, e.md5, sizeof(smeta.md5) - 1);
            smeta.md5[sizeof(smeta.md5) - 1] = '\0';

            if (src->source == PANEL_LOCAL && dst->source == PANEL_CLOUD)
                panel_copy_file(PANEL_LOCAL, NULL, srcPath, PANEL_CLOUD, &dst->cfg, dstPath,
                                e.name, &smeta, dst_exists ? &dmeta : NULL, &ctx);
            else if (src->source == PANEL_CLOUD && dst->source == PANEL_LOCAL)
                panel_copy_file(PANEL_CLOUD, &src->cfg, srcPath, PANEL_LOCAL, NULL, dstPath,
                                e.name, &smeta, dst_exists ? &dmeta : NULL, &ctx);
            else
                /* Local -> Local: no dedicated helper in the shared core
                 * (the Qt frontend uses QFile/QDir for this instead of
                 * touching it) — v1 Amiga scope just doesn't offer
                 * Local<->Local copy; both panels pointed at Local is a
                 * rare Commander configuration and can go through
                 * Workbench/Shell meanwhile. */
                mui_notify(UI_WARN, "Local->Local-Kopieren wird auf dem Amiga (noch) nicht angeboten.");
        }
    }

    g_op = NULL;
    end_operation();

    /* Only the destination panel's listing changed — reloading src too is
     * pointless (an extra remote `list` round-trip on every download, or a
     * pointless re-read on every upload). */
    panel_mui_reload(dst);

    snprintf(result_msg, sizeof(result_msg), "%d ok, %d Fehler", ctx.pr.ok, ctx.pr.err);
    mui_notify(UI_INFO, result_msg);
}

/* F6 / "Verschieben" button — cross-panel Local<->Cloud move only. Renaming
 * in place used to be bolted onto this same action (decided implicitly by
 * whether the two panels' sources matched), which made the confirm
 * requester ask "Objekt verschieben?" even when the user just meant to
 * rename — see do_rename() below for the now-separate action. */
static void do_move(void)
{
    AmigaPanel *src = g_active;
    AmigaPanel *dst = other_panel();
    PanelEntry e;
    char srcPath[AC_MAX_PATH], dstPath[AC_MAX_PATH];

    if (!panel_mui_current_entry(src, &e)) return;

    if (src->source == dst->source) {
        mui_notify(UI_WARN, "Verschieben braucht zwei verschiedene Panels (lokal<->AmiCloud). Umbenennen: Rechte Amiga+R.");
        return;
    }

    /* Cross-panel: Local<->Cloud move = copy + delete source. */
    if (!mui_confirm("Objekt verschieben?")) return;

    panel_mui_child_path(src, &e, srcPath, sizeof(srcPath));
    panel_mui_child_path(dst, &e, dstPath, sizeof(dstPath));

    begin_operation();
    if (src->source == PANEL_LOCAL) {
        if (e.is_dir) {
            /* ask=0: a move's destination is freshly derived from the
             * source name, no user-facing overwrite decision to make
             * here (matches this function's existing silent-merge
             * behaviour). */
            CopyCtx c;
            copyctx_init(&c, 0);
            count_local_tree(srcPath, &c.pr);
            panel_put_dir(&dst->cfg, srcPath, dstPath, &c);
            /* No recursive local delete in v1 (see do_delete()) — the
             * copy succeeded, but the source directory stays put. */
            mui_notify(UI_WARN, "Kopiert, aber lokales Verzeichnis nicht geloescht (v1-Einschraenkung).");
        } else {
            cmd_put(&dst->cfg, srcPath, dstPath);
            DeleteFile((CONST_STRPTR)srcPath);
        }
    } else {
        if (e.is_dir) {
            CopyCtx c;
            copyctx_init(&c, 0);
            count_remote_tree(&src->cfg, srcPath, &c.pr);
            panel_get_dir(&src->cfg, srcPath, dstPath, &c);
            cmd_del_recursive(&src->cfg, srcPath);
        } else {
            cmd_get(&src->cfg, srcPath, dstPath);
            cmd_del(&src->cfg, srcPath);
        }
    }
    end_operation();
    panel_mui_reload(&g_left);
    panel_mui_reload(&g_right);
}

/* Rechte Amiga+R / Projekt-Menu "Umbenennen" — plain rename in place,
 * split out of do_move() (see comment there). */
static void do_rename(void)
{
    AmigaPanel *src = g_active;
    PanelEntry e;
    char srcPath[AC_MAX_PATH], dstPath[AC_MAX_PATH];
    char newName[256];

    if (!panel_mui_current_entry(src, &e)) return;
    if (!ask_string("Umbenennen", e.name, newName, sizeof(newName))) return;
    if (!newName[0] || strcmp(newName, e.name) == 0) return;

    panel_mui_child_path(src, &e, srcPath, sizeof(srcPath));
    {
        PanelEntry renamed = e;
        strncpy(renamed.name, newName, sizeof(renamed.name) - 1);
        renamed.uname[0] = '\0';   /* typed: converted for the server */
        panel_mui_child_path(src, &renamed, dstPath, sizeof(dstPath));
    }

    if (src->source == PANEL_CLOUD) {
        begin_operation();
        cmd_rename(&src->cfg, srcPath, dstPath);
        end_operation();
    } else {
        Rename((CONST_STRPTR)srcPath, (CONST_STRPTR)dstPath);
    }
    panel_mui_reload(src);
}

static void do_mkdir(void)
{
    char name[256];
    char path[AC_MAX_PATH];

    if (!ask_string("Verzeichnis anlegen", "", name, sizeof(name))) return;
    if (!name[0]) return;

    {
        PanelEntry tmp;
        memset(&tmp, 0, sizeof(tmp));
        strncpy(tmp.name, name, sizeof(tmp.name) - 1);
        panel_mui_child_path(g_active, &tmp, path, sizeof(path));
    }

    if (g_active->source == PANEL_CLOUD) {
        begin_operation();
        cmd_mkdir(&g_active->cfg, path);
        end_operation();
    } else {
        localfs_mkdir(path);
    }
    panel_mui_reload(g_active);
}

static void do_delete(void)
{
    PanelEntry e;
    char path[AC_MAX_PATH];

    if (!panel_mui_current_entry(g_active, &e)) return;
    if (!mui_confirm("Objekt endgueltig loeschen?")) return;

    panel_mui_child_path(g_active, &e, path, sizeof(path));

    begin_operation();
    if (g_active->source == PANEL_CLOUD) {
        if (e.is_dir) cmd_del_recursive(&g_active->cfg, path);
        else          cmd_del(&g_active->cfg, path);
    } else {
        /* Recursive local delete has no ready-made helper in localfs.h
         * (the Qt frontend uses QDir::removeRecursively() instead) — a
         * non-empty directory here just fails silently via DeleteFile(),
         * matching the "no support yet" note in do_copy() above. */
        DeleteFile((CONST_STRPTR)path);
    }
    end_operation();
    panel_mui_reload(g_active);
}

static void do_about(void)
{
    /* Human-facing text — no literal "$VER:" prefix (that string is only
     * for AmigaOS's Version command to scan for, see amiga_ver[] in
     * main_mui.c and MUIA_Application_Version above). */
    MUI_Request(g_app, NULL, 0, "Ueber AmiCloud Commander", "_OK",
               "AmiCloud Commander\nDual-pane AmiCloud file manager\n\n"
               "Version " AC_VERSION "\n(C) 2026 Ronny Krueger\n"
               "GNU General Public License v3 or later");
}

/* Prüfsumme anzeigen — selection in the active panel, up to 20 lines.
 * Cloud entries already carry their MD5 from the listing; local entries
 * get it computed on demand (md5_file_hex(), same helper the shell's
 * lmd5 command uses). */
static void do_checksum(void)
{
    LONG   cookie = MUIV_List_NextSelected_Start;
    PanelEntry e;
    char   msg[2048];
    size_t used = 0;
    int    shown = 0, extra = 0;

    msg[0] = '\0';
    if (!panel_mui_next_selected(g_active, &cookie, &e)) return;

    cookie = MUIV_List_NextSelected_Start;
    gui_busy_begin();
    while (panel_mui_next_selected(g_active, &cookie, &e)) {
        char path[AC_MAX_PATH], line[320], hex[33] = "";

        panel_mui_child_path(g_active, &e, path, sizeof(path));
        if (e.is_dir) {
            snprintf(line, sizeof(line), "%s: (Verzeichnis)\n", e.name);
        } else if (g_active->source == PANEL_CLOUD) {
            snprintf(line, sizeof(line), "%s: %s\n", e.name, e.md5[0] ? e.md5 : "(unbekannt)");
        } else if (md5_file_hex(path, hex)) {
            snprintf(line, sizeof(line), "%s: %s\n", e.name, hex);
        } else {
            snprintf(line, sizeof(line), "%s: (Fehler)\n", e.name);
        }

        if (shown < 20) {
            size_t linelen = strlen(line);
            if (used + linelen < sizeof(msg) - 1) {
                strcpy(msg + used, line);
                used += linelen;
            }
            shown++;
        } else {
            extra++;
        }
    }
    gui_busy_end();

    if (extra) {
        char tail[64];
        size_t taillen;
        snprintf(tail, sizeof(tail), "... und %d weitere\n", extra);
        taillen = strlen(tail);
        if (used + taillen < sizeof(msg) - 1) { strcpy(msg + used, tail); used += taillen; }
    }
    if (!used) strcpy(msg, "(keine Auswahl)");

    MUI_Request(g_app, NULL, 0, "Pruefsumme", "_OK", msg);
}

/* ------------------------------------------------------------------ */
/* Suchen (F4) — docs/design/search-plan.md section 5, MUI part.  */
/* v1 scope: a single ask_string() requester for the pattern only (no    */
/* "ganze Cloud"/"auch Ordner" choices like the Qt requester) — the      */
/* default scope is always the active panel's current path, recursive,  */
/* directories included, matching the CLI's cmd_search_print(). */
/* ------------------------------------------------------------------ */
static char g_search_disp_path[1024];
static char g_search_disp_size[16];
static char g_search_disp_date[24];

static int g_search_cloud;   /* the hits are server paths (UTF-8) */

static __saveds LONG search_display_func(__reg("a0") struct Hook *hook,
                                          __reg("a2") char **array,
                                          __reg("a1") SearchHit *hit)
{
    (void)hook;

    if (!hit) {
        array[0] = "Pfad";
        array[1] = "Groesse";
        array[2] = "Datum";
        return 0;
    }

    {
        /* Phase 4: cloud hits are server paths (UTF-8) */
        char lp[AC_MAX_PATH];
        if (g_search_cloud) cs_from_utf8(hit->path, lp, sizeof(lp));
        else { strncpy(lp, hit->path, sizeof(lp) - 1); lp[sizeof(lp) - 1] = '\0'; }
        if (hit->is_dir)
            snprintf(g_search_disp_path, sizeof(g_search_disp_path), "%s/", lp);
        else {
            strncpy(g_search_disp_path, lp, sizeof(g_search_disp_path) - 1);
            g_search_disp_path[sizeof(g_search_disp_path) - 1] = '\0';
        }
    }
    array[0] = g_search_disp_path;

    if (hit->is_dir) strcpy(g_search_disp_size, "(dir)");
    else             sprintf(g_search_disp_size, "%ld", hit->size);
    array[1] = g_search_disp_size;

    if (hit->mtime > 0) {
        int y, mo, d;
        ac_civil_from_days(hit->mtime / 86400, &y, &mo, &d);
        sprintf(g_search_disp_date, "%04d-%02d-%02d", y, mo, d);
    } else {
        g_search_disp_date[0] = '\0';
    }
    array[2] = g_search_disp_date;
    return 0;
}
static struct Hook search_display_hook = { { NULL, NULL }, (APTR)search_display_func, NULL, NULL };

/* search_local()'s callback -- appends into the fixed g_search_hits
 * buffer, capped at SEARCH_MAX_HITS (see its definition above). */
typedef struct { SearchHit *arr; int n; } SearchCollectCtx;

static void search_collect_cb(const SearchHit *hit, void *ud)
{
    SearchCollectCtx *ctx = (SearchCollectCtx *)ud;
    if (ctx->n >= SEARCH_MAX_HITS) return;
    ctx->arr[ctx->n++] = *hit;
}

static void do_search(void)
{
    char pattern[256];
    int  i;

    if (!ask_string("Suchmuster:", "", pattern, sizeof(pattern))) return;
    if (!pattern[0]) return;

    /* Einzelinstanz: vorherige Treffer verwerfen, dieselbe Liste neu
     * befuellen statt ein zweites Fenster zu oeffnen. */
    set(g_search_list, MUIA_List_Quiet, TRUE);
    DoMethod(g_search_list, MUIM_List_Clear);
    g_search_n = 0;

    if (!g_search_hits) g_search_hits = (SearchHit *)malloc(sizeof(SearchHit) * SEARCH_MAX_HITS);
    if (!g_search_hits) { set(g_search_list, MUIA_List_Quiet, FALSE); return; }

    if (g_active->source == PANEL_CLOUD) {
        AcSearchHit *raw = NULL;
        int n = 0, trunc = 0, ok;

        char upat[256];
        cs_to_utf8(pattern, upat, sizeof(upat));   /* Phase 4: typed locally */
        g_search_cloud = 1;
        gui_busy_begin();
        ok = cmd_search(&g_active->cfg, g_active->path, upat, 1, &raw, &n, &trunc);
        gui_busy_end();

        if (ok) {
            if (n > SEARCH_MAX_HITS) n = SEARCH_MAX_HITS;
            for (i = 0; i < n; i++) {
                strncpy(g_search_hits[i].path, raw[i].path, sizeof(g_search_hits[i].path) - 1);
                g_search_hits[i].path[sizeof(g_search_hits[i].path) - 1] = '\0';
                g_search_hits[i].is_dir = raw[i].is_dir;
                g_search_hits[i].size   = raw[i].size;
                g_search_hits[i].mtime  = raw[i].mtime;
            }
            g_search_n = n;
        }
        cmd_search_free(raw);
    } else {
        SearchCollectCtx ctx;
        g_search_cloud = 0;
        ctx.arr = g_search_hits;
        ctx.n   = 0;
        search_local(g_active->path, pattern, search_collect_cb, &ctx);
        g_search_n = ctx.n;
    }

    for (i = 0; i < g_search_n; i++) {
        DoMethod(g_search_list, MUIM_List_InsertSingle, &g_search_hits[i], MUIV_List_Insert_Bottom);
    }
    set(g_search_list, MUIA_List_Quiet, FALSE);
    DoMethod(g_search_list, MUIM_List_Redraw, MUIV_List_Redraw_All);

    set(g_search_window, MUIA_Window_Open, TRUE);
    DoMethod(g_search_window, MUIM_Window_ToFront);
}

/* Doppelklick/"Gehe zu": Pfad des aktuellen Treffers in Ordner+Basename
 * splitten, das aktive Panel dorthin navigieren (reload passiert schon in
 * panel_mui_set_cloud/set_local()) und die Zeile markieren. */
static void do_search_goto(void)
{
    LONG active = -1;
    char pathbuf[1024], dir[AC_MAX_PATH], name[256];
    char *cut;
    int  i, idx;

    if (!g_search_hits || g_search_n == 0) return;
    get(g_search_list, MUIA_List_Active, &active);
    if (active < 0 || active >= g_search_n) return;

    strncpy(pathbuf, g_search_hits[active].path, sizeof(pathbuf) - 1);
    pathbuf[sizeof(pathbuf) - 1] = '\0';

    cut = strrchr(pathbuf, '/');
    if (cut) {
        *cut = '\0';
        strncpy(name, cut + 1, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        strncpy(dir, pathbuf, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        if (!dir[0] && g_active->source == PANEL_CLOUD) strcpy(dir, "/");
    } else {
        strncpy(name, pathbuf, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        dir[0] = '\0';
    }

    if (g_active->source == PANEL_CLOUD) {
        char lname[256];
        panel_mui_set_cloud(g_active, dir);
        cs_from_utf8(name, lname, sizeof(lname));   /* list names are local */
        strncpy(name, lname, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    } else {
        panel_mui_set_local(g_active, dir);
    }

    idx = -1;
    for (i = 0; i < g_active->list.n; i++) {
        if (strcmp(g_active->list.items[i].name, name) == 0) { idx = i; break; }
    }
    if (idx >= 0) set(g_active->list_obj, MUIA_List_Active, idx);
}

/* ------------------------------------------------------------------ */
/* Main window menu — About/Pruefsumme/Beenden. Picked items surface     */
/* through the same MUIM_Application_Input/switch(id) loop as buttons,    */
/* via MUI_MakeObject(MUIO_MenustripNM, ...) below (mui38dev/MUI/Developer/ */
/* C/Examples/Menus.c is the reference pattern this follows).             */
/* ------------------------------------------------------------------ */
static struct NewMenu MenuData[] = {
    { NM_TITLE, "Projekt", 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Ueber AmiCloud Commander...", "?", 0, 0, (APTR)ID_ABOUT },
    { NM_ITEM,  NM_BARLABEL, 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Pruefsumme anzeigen", "M", 0, 0, (APTR)ID_CHECKSUM },
    { NM_ITEM,  "Umbenennen", "R", 0, 0, (APTR)ID_RENAME },
    { NM_ITEM,  "Suchen...", "S", 0, 0, (APTR)ID_SEARCH },
    { NM_ITEM,  NM_BARLABEL, 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Beenden", "Q", 0, 0, (APTR)ID_QUIT },
    { NM_END, NULL, 0, 0, 0, (APTR)0 },
};

int gui_run(const AcConfig *cfg)
{
    Object *bt_reload, *bt_info, *bt_search, *bt_copy, *bt_move, *bt_mkdir, *bt_delete, *bt_quit;
    Object *window;
    Object *search_goto_button, *search_close_button;
    ULONG   sigs = 0;
    BOOL    running = TRUE;

    panel_mui_build(&g_left, cfg, NULL);
    panel_mui_build(&g_right, cfg, NULL);

    g_main_status = TextObject, MUIA_Text_Contents, "", End;
    /* Right-hand half of the status row (see gui_busy_begin()/_end() in
     * gui.h) — empty and invisible-by-content whenever no cloud
     * round-trip is in flight, "Online-Zugriff..." while one is, so a
     * slow/serial-attached Amiga has a visible "it's not hung" signal in
     * the window itself, not just the MUI busy pointer (which is easy to
     * miss if the mouse hasn't moved). */
    g_busy_text   = TextObject, MUIA_Text_Contents, "", MUIA_Text_PreParse, "\033b", End;

    /* No leading "_" here on purpose: MUIA_ControlChar only reacts to
     * Amiga+<letter>, never to the actual F-key printed in the label, and
     * every one of these previously collided on the same control char
     * 'F' anyway. The real F-key shortcuts are wired further below via
     * MUIA_Window_InputEvent notifications, these labels are just a
     * legend. */
    bt_reload  = SimpleButton("F2 Neu laden");
    bt_info    = SimpleButton("F3 Info");
    bt_search  = SimpleButton("F4 Suchen");
    bt_copy    = SimpleButton("F5 Kopieren");
    bt_move    = SimpleButton("F6 Verschieben");
    bt_mkdir   = SimpleButton("F7 MkDir");
    bt_delete  = SimpleButton("F8 Loeschen");
    bt_quit    = SimpleButton("F10 Beenden");

    /* Suchergebnisse (docs/design/search-plan.md section 5) — built
     * upfront and OM_ADDMEMBER'd below like g_progress_window/g_input_window,
     * so it can be opened/closed via set(MUIA_Window_Open) alone, reused
     * across searches (do_search() just clears+refills g_search_list). */
    g_search_list = ListObject,
                        InputListFrame,
                        MUIA_List_Title,       TRUE,
                        MUIA_List_Format,      ",BAR,,BAR,",
                        MUIA_List_DisplayHook, &search_display_hook,
                    End;
    g_search_listview = ListviewObject, MUIA_Listview_List, g_search_list, End;
    search_goto_button  = SimpleButton("_Gehe zu");
    search_close_button = SimpleButton("_Schliessen");
    g_search_window = WindowObject,
        MUIA_Window_Title, "Suchergebnisse",
        MUIA_Window_Width, MUIV_Window_Width_Screen(50),
        MUIA_Window_Height, MUIV_Window_Height_Screen(50),
        WindowContents, VGroup,
            Child, g_search_listview,
            Child, HGroup,
                Child, search_goto_button,
                Child, search_close_button,
            End,
        End,
    End;

    g_progress_text  = TextObject, MUIA_Text_Contents, "", End;
    /* Overall x/y file counter across the whole copy — done*100/total as
     * the fill, "x/y" as the overlaid info text; see mui_progress()/g_op.
     * Lives in this same window as the Cancel button, not the main
     * window's status line, per the copy-flow rework (user complaint: the
     * old per-file "(i/count) name" line was in the main window, easy to
     * miss, while the actual progress feedback belongs where Abbrechen is).
     * GaugeFrame gives it the theme's native recessed gauge look instead
     * of a bare unframed bar (2nd complaint: "eine huebsche Progressbar"). */
    g_progress_gauge = GaugeObject, GaugeFrame, MUIA_Gauge_Horiz, TRUE, MUIA_Gauge_Max, 100,
                       MUIA_Gauge_InfoText, "", End;
    g_cancel_button  = SimpleButton("_Abbrechen");
    g_progress_window = WindowObject,
        MUIA_Window_Title, "Uebertrage...",
        MUIA_Window_CloseGadget, FALSE,
        MUIA_Window_Menu, MUIV_Window_Menu_NoMenu,
        /* Was shrink-wrapped to the "Abbrechen" button's width by default
         * (no child requests anything wider) — pin it to roughly a third
         * of the screen instead so the gauge/text/filename actually have
         * room to breathe. */
        MUIA_Window_Width, MUIV_Window_Width_Screen(33),
        WindowContents, VGroup,
            Child, g_progress_text,
            Child, g_progress_gauge,
            Child, g_cancel_button,
        End,
    End;

    g_app = ApplicationObject,
        MUIA_Application_Title,       "AmiCloud Commander",
        MUIA_Application_Version,     "$VER: amicloud-commander " AC_VERSION " (" AC_DATE ")",
        MUIA_Application_Copyright,   "(C) 2026 Ronny Krueger",
        MUIA_Application_Author,      "Ronny Krueger",
        MUIA_Application_Description, "Dual-pane AmiCloud file manager",
        MUIA_Application_Base,        "AMICLOUDCOMMANDER",

        SubWindow, window = WindowObject,
            MUIA_Window_ID,    MAKE_ID('M','A','I','N'),
            MUIA_Window_Title, "AmiCloud Commander",
            MUIA_Window_Menustrip, MUI_MakeObject(MUIO_MenustripNM, MenuData, 0),
            WindowContents, VGroup,
                Child, HGroup,
                    Child, g_left.group,
                    Child, g_right.group,
                End,
                Child, HGroup,
                    Child, bt_reload,
                    Child, bt_info,
                    Child, bt_search,
                    Child, bt_copy,
                    Child, bt_move,
                    Child, bt_mkdir,
                    Child, bt_delete,
                    Child, bt_quit,
                End,
                Child, HGroup,
                    Child, g_main_status,
                    Child, g_busy_text,
                End,
            End,
        End,
    End;

    if (!g_app) {
        return 20;
    }
    g_main_window = window; /* see gui_busy_begin() */

    /* Installed once for the whole run — see the comment on
     * begin_operation() above for why this must not be scoped to
     * individual operations. */
    ui_set_hooks(&g_mui_hooks);

    /* g_progress_window was built before g_app existed and is only ever
     * opened/closed via set(MUIA_Window_Open) in begin_operation()/
     * end_operation() — without being a member of the Application it has
     * no owning app to open through, so it never actually appears while
     * MUIA_Application_Sleep still freezes the main window, looking like
     * a hang for every begin_operation()-wrapped action (mkdir, copy,
     * rename, delete). Same OM_ADDMEMBER pattern build_input_dialog()
     * already uses for g_input_window. */
    DoMethod(g_app, OM_ADDMEMBER, g_progress_window);

    /* Suchergebnisfenster — same OM_ADDMEMBER reasoning as g_progress_window
     * above: only ever opened/closed via set(MUIA_Window_Open), reused
     * across searches, never rebuilt (docs/design/search-plan.md). */
    DoMethod(g_app, OM_ADDMEMBER, g_search_window);
    DoMethod(g_search_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            g_search_window, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(search_close_button, MUIM_Notify, MUIA_Pressed, FALSE,
            g_search_window, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(search_goto_button, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_SEARCH_GOTO);
    DoMethod(g_search_listview, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_SEARCH_GOTO);

    build_input_dialog();

    DoMethod(window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_QUIT);

    /* Real F-key shortcuts: MUIA_Window_InputEvent notifications match a
     * commodities.library input-description string (MUI_Window.doc), NOT
     * MUIV_EveryTime — "-fN" is "fN, up-transition", i.e. fires once per
     * keypress, no autorepeat flood. (MUIA_ControlChar, used for the
     * buttons' plain-text labels below, only reacts to Amiga+<letter>,
     * never to the actual F-keys — hence this separate mechanism.) Each
     * posts the same MUIM_Application_ReturnID its corresponding button
     * posts, so it runs through the exact same main-loop switch() below —
     * no duplicated action logic. */
    DoMethod(window, MUIM_Notify, MUIA_Window_InputEvent, "-f2",
            g_app, 2, MUIM_Application_ReturnID, ID_RELOAD);
    DoMethod(window, MUIM_Notify, MUIA_Window_InputEvent, "-f3",
            g_app, 2, MUIM_Application_ReturnID, ID_INFO);
    DoMethod(window, MUIM_Notify, MUIA_Window_InputEvent, "-f4",
            g_app, 2, MUIM_Application_ReturnID, ID_SEARCH);
    DoMethod(window, MUIM_Notify, MUIA_Window_InputEvent, "-f5",
            g_app, 2, MUIM_Application_ReturnID, ID_COPY);
    DoMethod(window, MUIM_Notify, MUIA_Window_InputEvent, "-f6",
            g_app, 2, MUIM_Application_ReturnID, ID_MOVE);
    DoMethod(window, MUIM_Notify, MUIA_Window_InputEvent, "-f7",
            g_app, 2, MUIM_Application_ReturnID, ID_MKDIR);
    DoMethod(window, MUIM_Notify, MUIA_Window_InputEvent, "-f8",
            g_app, 2, MUIM_Application_ReturnID, ID_DELETE);
    DoMethod(window, MUIM_Notify, MUIA_Window_InputEvent, "-f10",
            g_app, 2, MUIM_Application_ReturnID, ID_QUIT);

    DoMethod(bt_quit, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_QUIT);
    DoMethod(bt_reload, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_RELOAD);
    DoMethod(bt_info, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_INFO);
    DoMethod(bt_search, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_SEARCH);
    DoMethod(bt_copy, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_COPY);
    DoMethod(bt_move, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_MOVE);
    DoMethod(bt_mkdir, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_MKDIR);
    DoMethod(bt_delete, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_DELETE);
    DoMethod(g_cancel_button, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_CallHook, &cancel_hook);

    panel_mui_wire_notifies(&g_left,  g_app, ID_LEFT_SOURCE,  ID_LEFT_GO,  ID_LEFT_DCLICK,  ID_LEFT_UP,  ID_LEFT_SELECT);
    panel_mui_wire_notifies(&g_right, g_app, ID_RIGHT_SOURCE, ID_RIGHT_GO, ID_RIGHT_DCLICK, ID_RIGHT_UP, ID_RIGHT_SELECT);
    /* "Which panel is active" has no cheap MUI focus query in v1 (see
     * file header) — double-click/Up/source-switch on a panel doubles as
     * declaring it active, matching where the user is already looking. */

    {
        char localCwd[AC_MAX_PATH];
        if (!localfs_getcwd(localCwd, sizeof(localCwd))) strcpy(localCwd, "");
        panel_mui_set_local(&g_left, localCwd);
        panel_mui_set_cloud(&g_right, cfg->basepath[0] ? cfg->basepath : "/");
    }

    set_active(&g_left); /* applies the initial highlight, see set_active() */
    set(window, MUIA_Window_Open, TRUE);

    while (running) {
        LONG id = DoMethod(g_app, MUIM_Application_Input, &sigs);
        switch (id) {
        case ID_QUIT:        running = FALSE; break;
        case ID_RELOAD:      do_reload(); break;
        case ID_INFO:        do_info(); break;
        case ID_COPY:        do_copy(); break;
        case ID_MOVE:        do_move(); break;
        case ID_MKDIR:       do_mkdir(); break;
        case ID_DELETE:      do_delete(); break;
        case ID_ABOUT:       do_about(); break;
        case ID_CHECKSUM:    do_checksum(); break;
        case ID_RENAME:      do_rename(); break;
        case ID_SEARCH:      do_search(); break;
        case ID_SEARCH_GOTO: do_search_goto(); break;

        case ID_LEFT_SOURCE:  set_active(&g_left);  panel_mui_source_changed(&g_left);  break;
        case ID_LEFT_GO:      set_active(&g_left);  panel_mui_path_entered(&g_left);    break;
        case ID_LEFT_DCLICK:  set_active(&g_left);  panel_mui_enter_selected(&g_left);  break;
        case ID_LEFT_UP:      set_active(&g_left);  panel_mui_go_up(&g_left);           break;
        case ID_LEFT_SELECT:  set_active(&g_left);                                      break;

        case ID_RIGHT_SOURCE: set_active(&g_right); panel_mui_source_changed(&g_right); break;
        case ID_RIGHT_GO:     set_active(&g_right); panel_mui_path_entered(&g_right);   break;
        case ID_RIGHT_DCLICK: set_active(&g_right); panel_mui_enter_selected(&g_right); break;
        case ID_RIGHT_UP:     set_active(&g_right); panel_mui_go_up(&g_right);          break;
        case ID_RIGHT_SELECT: set_active(&g_right);                                     break;

        default: break;
        }
        /* A Ctrl-C (only possible when started from a Shell) cancels the
         * transfer it hit, see ui_hooks.h -- never every later one. */
        ui_break_clear();
        if (running && sigs) Wait(sigs);
    }

    set(window, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(g_app);
    return 0;
}
