/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
#ifndef GUI_H
#define GUI_H

/*
 * gui.h — AmiCloud Commander, Amiga/MUI frontend (GUI-Commander-plan
 * Phase 2, section 7). Same panel.h/cmd.h/ui_hooks.h core as the CLI,
 * shell and Qt Commander — this file only adds the MUI object tree and
 * glue around it.
 *
 * IMPORTANT — unverified: written against the MUI 3.8 Developer SDK
 * headers/examples (mui38dev/) using vbcc's native callback syntax
 * (__saveds, __reg("aN") — not SAS/C's __asm/register __aN, which vbcc's
 * parser rejects), but there is no VBCC/MUI toolchain in the environment
 * this was written in, so none of this directory's MUI code has been compiled or
 * run beyond fixing the errors from one real build attempt. Needs a
 * native build pass on real (or emulated) AmigaOS 3.x hardware — see plan
 * section 12.
 */

#include <exec/types.h>
#include <intuition/classes.h>

#include "panel.h"

/* One side of the two-pane window — mirrors PanelWidget from the Qt
 * frontend (apps/amicloud-commander-qt/src/panelwidget.h), MUI objects instead of Qt widgets. */
typedef struct {
    PanelSource source;
    AcConfig    cfg;
    char        path[AC_MAX_PATH];
    char        local_path[AC_MAX_PATH];   /* remembered across source switches */
    char        cloud_path[AC_MAX_PATH];
    PanelList   list;
    int         show_up;                    /* ".." synthetic row 0 in the list? */

    Object *group;         /* outer VGroup — what MainWindow adds as a Child */
    Object *source_cycle;
    Object *path_string;
    Object *up_button;
    Object *listview;
    Object *list_obj;
    Object *status_text;
} AmigaPanel;

/* Builds one panel's MUI object group. Does NOT wire notifies or read
 * any directory yet — call panel_mui_wire_notifies() once the owning
 * Application object exists, then panel_mui_set_local()/set_cloud() to
 * do the first read. cfg is copied in (shared server auth, like Qt's
 * MainWindow::m_cfg). */
void panel_mui_build(AmigaPanel *p, const AcConfig *cfg, Object *app);

/* Wires the Cycle/path-string/list-double-click/Up-button notifies to
 * fixed MUIM_Application_ReturnID values gui.c's main loop dispatches
 * on — see panel_mui.c's file comment for why this (not a custom hook)
 * is the interaction model. */
void panel_mui_wire_notifies(AmigaPanel *p, Object *app,
                             LONG id_source, LONG id_go, LONG id_dclick, LONG id_up,
                             LONG id_select);

void panel_mui_set_local(AmigaPanel *p, const char *path);
void panel_mui_set_cloud(AmigaPanel *p, const char *path);
void panel_mui_source_changed(AmigaPanel *p);
void panel_mui_path_entered(AmigaPanel *p);
void panel_mui_reload(AmigaPanel *p);
void panel_mui_go_up(AmigaPanel *p);
void panel_mui_enter_selected(AmigaPanel *p);
void panel_mui_child_path(const AmigaPanel *p, const PanelEntry *e, char *out, size_t outsz);

/* The entry under the list cursor, or the first multi-selected entry if
 * any are selected (MUIA_List_Active / manual scan for TRUE Selected
 * flags — see panel_mui.c). Returns 0 if the panel is empty or only the
 * synthetic ".." row is current. */
int panel_mui_current_entry(const AmigaPanel *p, PanelEntry *out);

/* Multi-select iteration (shift/ctrl-click range or individual marks) —
 * degrades to just the active/cursor entry when nothing is explicitly
 * marked, see panel_mui.c. *cookie must start at MUIV_List_NextSelected_Start. */
int panel_mui_next_selected(const AmigaPanel *p, LONG *cookie, PanelEntry *out);

/* Busy indicator for any network round-trip: switches on the MUI busy
 * pointer (via MUIA_Application_Sleep) and a short status text in the
 * main window. begin_operation()/end_operation() (gui.c) already do this
 * for the F5/F6/F7/F8 transfer operations; these are the same mechanism
 * exposed so panel_mui_reload() and other single cloud round-trips (F3
 * Info) get the same visible feedback even though they're too short to
 * have a chunked ui_progress() callback to hang a Cancel button off of.
 * Nestable (depth counter), so callers don't need to know whether they're
 * already inside a begin_operation() — e.g. mui_overwrite() backs the
 * sleep off around its own nested MUI_Request and back on afterwards. */
void gui_busy_begin(void);
void gui_busy_end(void);

/* Main window assembly + event loop (gui.c). Returns a process exit code. */
int gui_run(const AcConfig *cfg);

#endif /* GUI_H */
