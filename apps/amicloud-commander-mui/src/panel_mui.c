/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
/*
 * panel_mui.c — MUI side of one Commander panel (see gui.h). Builds the
 * Cycle/String/Listview group and keeps it in sync with a PanelList from
 * panel.h. UNVERIFIED — see the notice in gui.h.
 *
 * Notify wiring is deliberately the plainest possible MUI idiom: every
 * user action (source switch, path Return, list double-click, Up
 * button) just posts a fixed numeric ID via MUIM_Application_ReturnID,
 * exactly like every MUI example in mui38dev/MUI/Developer/C/Examples/
 * (e.g. EnvBrowser.c's window-close/list-double-click notifies) — no
 * custom hooks, no per-instance hook-data passing to get wrong without a
 * way to test it.
 */
#include <libraries/mui.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>

#include "gui.h"
#include "localfs.h"
#include "mui_stubs.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cmd.h"        /* cmd_status_ex() */
#include "progress.h"   /* ac_format_kb() */

/* ------------------------------------------------------------------ */
/* List display hook: NULL entry means "give me the column titles"      */
/* (MUIA_List_Title, see MUI_List.doc/MUIA_List_DisplayHook example).    */
/* Static buffers are safe here — MUI renders one row at a time and      */
/* copies the strings out before the next hook call (documented idiom). */
/* ------------------------------------------------------------------ */
static char g_disp_size[16];
static char g_disp_date[24];

static __saveds LONG panel_display_func(__reg("a0") struct Hook *hook,
                                         __reg("a2") char **array,
                                         __reg("a1") PanelEntry *entry)
{
    (void)hook;

    if (!entry) {
        array[0] = "Name";
        array[1] = "Groesse";
        array[2] = "Datum";
        return 0;
    }

    array[0] = entry->name;
    if (entry->is_dir) {
        strcpy(g_disp_size, "(dir)");
    } else {
        sprintf(g_disp_size, "%ld", entry->size);
    }
    array[1] = g_disp_size;

    strncpy(g_disp_date, entry->modified, sizeof(g_disp_date) - 1);
    g_disp_date[sizeof(g_disp_date) - 1] = '\0';
    array[2] = g_disp_date;
    return 0;
}

static struct Hook panel_display_hook = { { NULL, NULL }, (APTR)panel_display_func, NULL, NULL };

/* ------------------------------------------------------------------ */
/* Panel construction                                                    */
/* ------------------------------------------------------------------ */
static const char *source_labels[] = { "Local", "AmiCloud", NULL };

void panel_mui_build(AmigaPanel *p, const AcConfig *cfg, Object *app)
{
    (void)app; /* notify wiring happens in panel_mui_wire_notifies(), below */

    memset(p, 0, sizeof(*p));
    p->cfg    = *cfg;
    p->source = PANEL_LOCAL;

    p->source_cycle = CycleObject, MUIA_Cycle_Entries, source_labels, End;
    p->path_string  = StringObject, StringFrame, End;
    p->up_button    = SimpleButton(".. (rauf)");
    p->list_obj     = ListObject,
                          InputListFrame,
                          MUIA_List_Title,       TRUE,
                          MUIA_List_Format,      ",BAR,,BAR,",
                          MUIA_List_DisplayHook, &panel_display_hook,
                      End;
    p->listview = ListviewObject,
                      MUIA_Listview_List, p->list_obj,
                      /* Shifted (not Default/user-prefs-dependent): plain
                       * click always selects/navigates a single entry,
                       * Shift+click always marks/unmarks individual (not
                       * necessarily adjacent) entries regardless of the
                       * user's MUI prefs — so several non-contiguous files
                       * can reliably be marked for F5 Copy. */
                      MUIA_Listview_MultiSelect, MUIV_Listview_MultiSelect_Shifted,
                  End;
    p->status_text = TextObject, MUIA_Text_Contents, "", End;

    p->group = VGroup,
                   Child, HGroup,
                       Child, p->source_cycle,
                       Child, p->path_string,
                       Child, p->up_button,
                   End,
                   Child, p->listview,
                   Child, p->status_text,
               End;
}

/* Hooks up this panel's interactive gadgets to fixed numeric IDs that
 * gui.c's main loop dispatches on. Call once per panel, after both
 * panel_mui_build() and the owning Application object exist.
 *
 * id_select fires on MUIA_List_Active, i.e. plain single-click selection
 * (or cursor movement) in the list — without it, g_active only follows
 * the Cycle/path-string/double-click/Up actions, so simply clicking a
 * row to select it in the *other* panel leaves g_active pointing at
 * whichever panel was last navigated, and F5 Copy/F6/F8 silently act on
 * the wrong side (looked like "copy to cloud does nothing" when the
 * cloud panel had been browsed last but a local file was then clicked). */
void panel_mui_wire_notifies(AmigaPanel *p, Object *app,
                             LONG id_source, LONG id_go, LONG id_dclick, LONG id_up,
                             LONG id_select)
{
    DoMethod(p->source_cycle, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
            app, 2, MUIM_Application_ReturnID, id_source);
    DoMethod(p->path_string, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
            app, 2, MUIM_Application_ReturnID, id_go);
    DoMethod(p->listview, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
            app, 2, MUIM_Application_ReturnID, id_dclick);
    DoMethod(p->up_button, MUIM_Notify, MUIA_Pressed, FALSE,
            app, 2, MUIM_Application_ReturnID, id_up);
    DoMethod(p->list_obj, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime,
            app, 2, MUIM_Application_ReturnID, id_select);
}

/* ------------------------------------------------------------------ */
void panel_mui_set_local(AmigaPanel *p, const char *path)
{
    p->source = PANEL_LOCAL;
    set(p->source_cycle, MUIA_Cycle_Active, 0);
    strncpy(p->local_path, path, sizeof(p->local_path) - 1);
    strncpy(p->path, path, sizeof(p->path) - 1);
    set(p->path_string, MUIA_String_Contents, p->path);
    panel_mui_reload(p);
}

void panel_mui_set_cloud(AmigaPanel *p, const char *path)
{
    p->source = PANEL_CLOUD;
    set(p->source_cycle, MUIA_Cycle_Active, 1);
    strncpy(p->cloud_path, path, sizeof(p->cloud_path) - 1);
    strncpy(p->path, path, sizeof(p->path) - 1);
    set(p->path_string, MUIA_String_Contents, p->path);
    panel_mui_reload(p);
}

/* Called when the source Cycle gadget itself fires (user clicked it) —
 * unlike set_local()/set_cloud() above (used for programmatic/initial
 * navigation), this restores whichever path was last shown for that
 * source rather than resetting it, matching the Qt frontend's
 * onSourceChanged() (panelwidget.cpp). */
void panel_mui_source_changed(AmigaPanel *p)
{
    LONG active = 0;
    get(p->source_cycle, MUIA_Cycle_Active, &active);

    if (active == 0) {
        if (!p->local_path[0]) {
            char buf[AC_MAX_PATH];
            if (localfs_getcwd(buf, sizeof(buf)))
                strncpy(p->local_path, buf, sizeof(p->local_path) - 1);
            else
                strcpy(p->local_path, "");
        }
        panel_mui_set_local(p, p->local_path);
    } else {
        if (!p->cloud_path[0])
            strncpy(p->cloud_path, p->cfg.basepath[0] ? p->cfg.basepath : "/",
                    sizeof(p->cloud_path) - 1);
        panel_mui_set_cloud(p, p->cloud_path);
    }
}

/* Called when the path string's Return is pressed — navigates to
 * whatever the user typed. */
void panel_mui_path_entered(AmigaPanel *p)
{
    STRPTR text = NULL;
    get(p->path_string, MUIA_String_Contents, &text);
    if (!text || !text[0]) return;

    if (p->source == PANEL_CLOUD) panel_mui_set_cloud(p, (char *)text);
    else                          panel_mui_set_local(p, (char *)text);
}

/* Cloud-Belegung fuer die Statuszeile ("1,2 MB von 5,0 GB"). Ein
 * zusaetzlicher status-Request kostet auf dem 68k einen TLS-Handshake,
 * daher hoechstens alle 30 s, gemeinsam fuer beide Panels. */
static long g_quota_time = 0;
static char g_quota_txt[48] = "";

static const char *cloud_quota_text(const AcConfig *cfg)
{
    long now = (long)time(NULL);
    if (g_quota_time == 0 || now - g_quota_time >= 30) {
        AcStatus st;
        g_quota_time = now;
        g_quota_txt[0] = '\0';
        if (cmd_status_ex(cfg, &st) && st.has_quota) {
            char used[24], total[24];
            ac_format_kb(st.quota_used_kb, used, sizeof(used));
            ac_format_kb(st.quota_total_kb, total, sizeof(total));
            if (st.quota_unlimited)
                sprintf(g_quota_txt, " - %s belegt", used);
            else
                sprintf(g_quota_txt, " - %s von %s belegt", used, total);
        }
    }
    return g_quota_txt;
}

void panel_mui_reload(AmigaPanel *p)
{
    int i;
    char statusbuf[96];
    int  ok;

    panel_list_free(&p->list);
    set(p->list_obj, MUIA_List_Quiet, TRUE);
    DoMethod(p->list_obj, MUIM_List_Clear);

    /* Cloud listing is a plain HTTP round-trip with no chunked
     * ui_progress() callback to hang feedback off of — without this the
     * window just sits there for however long that takes (every Cloud
     * navigation: source switch, Up, double-click into a dir, typed
     * path), which is exactly the "looks hung" complaint gui_busy_begin()
     * exists for (see gui.h). Local listing is effectively instant, no
     * need to toggle the busy pointer for it. */
    if (p->source == PANEL_CLOUD) gui_busy_begin();
    ok = panel_read(p->source, &p->cfg, p->path, &p->list);
    if (p->source == PANEL_CLOUD) gui_busy_end();

    if (!ok) {
        set(p->status_text, MUIA_Text_Contents, "Fehler beim Lesen");
        set(p->list_obj, MUIA_List_Quiet, FALSE);
        return;
    }
    panel_sort(&p->list, PANEL_SORT_NAME);

    for (i = 0; i < p->list.n; i++) {
        DoMethod(p->list_obj, MUIM_List_InsertSingle, &p->list.items[i], MUIV_List_Insert_Bottom);
    }
    set(p->list_obj, MUIA_List_Quiet, FALSE);
    DoMethod(p->list_obj, MUIM_List_Redraw, MUIV_List_Redraw_All);

    if (p->source == PANEL_CLOUD) {
        const char *q;
        gui_busy_begin();
        q = cloud_quota_text(&p->cfg);
        gui_busy_end();
        sprintf(statusbuf, "%ld Objekt(e)%s", (long)p->list.n, q);
    } else {
        sprintf(statusbuf, "%ld Objekt(e)", (long)p->list.n);
    }
    set(p->status_text, MUIA_Text_Contents, statusbuf);
}

void panel_mui_go_up(AmigaPanel *p)
{
    char resolved[AC_MAX_PATH];

    if (p->source == PANEL_CLOUD) {
        remote_resolve(resolved, sizeof(resolved), p->path, "..");
        panel_mui_set_cloud(p, resolved);
    } else {
        /* Local: reuse localfs_chdir()+getcwd() the way the shell does —
         * unlike the Qt frontend we have no QDir here, and localfs_amiga.c's
         * SetCurrentDirName()/GetCurrentDirName() pair is exactly this
         * idiom already (no manual Lock() juggling, see localfs_amiga.c).
         * AmigaDOS has no ".." — "/" is its "up one" (see shell.c's do_lcd). */
        if (localfs_chdir(p->path) && localfs_chdir("/") &&
            localfs_getcwd(resolved, sizeof(resolved))) {
            panel_mui_set_local(p, resolved);
        }
    }
}

void panel_mui_child_path(const AmigaPanel *p, const PanelEntry *e, char *out, size_t outsz)
{
    if (p->source == PANEL_CLOUD) {
        size_t len = strlen(p->path);
        if (len > 0 && p->path[len - 1] == '/')
            snprintf(out, outsz, "%s%s", p->path, e->name);
        else
            snprintf(out, outsz, "%s/%s", p->path, e->name);
    } else {
        localfs_join(out, outsz, p->path, e->name);
    }
}

int panel_mui_current_entry(const AmigaPanel *p, PanelEntry *out)
{
    LONG active = -1;
    get(p->list_obj, MUIA_List_Active, &active);
    if (active < 0 || active >= p->list.n) return 0;
    *out = p->list.items[active];
    return 1;
}

/* Iterates the list's own multi-select state (MUI_List.doc's
 * MUIM_List_NextSelected): every explicitly shift/ctrl-marked row, or —
 * when nothing is explicitly marked — just the active/cursor entry, so
 * callers get single-entry behaviour "for free" when the user hasn't
 * multi-selected anything. *cookie must be MUIV_List_NextSelected_Start
 * before the first call; returns 0 once iteration is finished. */
int panel_mui_next_selected(const AmigaPanel *p, LONG *cookie, PanelEntry *out)
{
    DoMethod(p->list_obj, MUIM_List_NextSelected, cookie);
    if (*cookie == MUIV_List_NextSelected_End) return 0;
    if (*cookie < 0 || *cookie >= p->list.n) return 0;
    *out = p->list.items[*cookie];
    return 1;
}

void panel_mui_enter_selected(AmigaPanel *p)
{
    PanelEntry e;
    char child[AC_MAX_PATH];

    if (!panel_mui_current_entry(p, &e)) return;
    if (!e.is_dir) return;

    panel_mui_child_path(p, &e, child, sizeof(child));
    if (p->source == PANEL_CLOUD) panel_mui_set_cloud(p, child);
    else                          panel_mui_set_local(p, child);
}
