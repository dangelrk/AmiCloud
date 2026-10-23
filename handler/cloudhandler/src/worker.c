/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Ronny Krueger
 */
/*
 * worker.c -- amicloud-worker: a normal AmigaOS process (full vbcc
 * startup, libc, bsdsocket/AmiSSL) that does all HTTP/TLS work on behalf
 * of the CLOUD: handler, which is exec.library-only and can never touch
 * the network stack itself (see handler.c's file header and
 * docs/design/handler-plan.md Stufe 2). Talks to the handler over proto.h's
 * WorkerMsg protocol through the well-known port AC_WORKER_PORT.
 *
 * Links libcloudhttp (cmd.c/http_amiga.c) + libcloudcore (parson.c/
 * base64.c/md5.c/config.c/ui_hooks.c/cloudtime.c) -- see Makefile.amiga-cross.
 * Before the M6 refactor (docs/design/05_Refactoring_Client_Handler.md)
 * this linked the old monorepo's flat client/ .c files directly; linking
 * the libraries (today core/, AmiCloudCore) is the only change here, the
 * dispatch logic below is unchanged.
 *
 * Stage 3 adds WREQ_SETCOMMENT/WREQ_SETPROTECT (cmd_setcomment()/
 * cmd_setprotect() in libcloudhttp) and proper gmt_offset/dst-aware date
 * conversion (libcloudcore's ac_epoch_to_datestamp()/ac_datestamp_to_epoch())
 * -- see docs/design/handler-plan.md Stufe 3.
 */
#ifdef PLATFORM_AMIGA

#include <dos/dos.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "proto.h"
#include "amicloud.h"
#include "cmd.h"
#include "config.h"
#include "http.h"
#include "ui_hooks.h"
#include "cloudtime.h" /* ac_datestamp_to_epoch/ac_epoch_to_datestamp --
                        * libcloudcore, the ONE place this conversion
                        * lives (module-encapsulation rule; used to be
                        * duplicated here and in libcloudfs/src/localfs_amiga.c). */

extern int ac_errors_to_stderr; /* http_amiga.c -- see apps/amicloud-commander-mui/src/main_mui.c for the same pattern */

const char amiga_ver[] = "$VER: amicloud-worker " AC_VERSION " (27.09.2026)";

static AcConfig g_cfg;
static int      g_net_ready = 0;

/* Quota for ACTION_INFO (docs/design/user-management-plan.md §12.9): the
 * handler must never block Workbench on a status request of its own.
 * Instead the worker fetches action=status right after a successful
 * WREQ_LIST -- at most every QUOTA_INTERVAL seconds, or sooner after a
 * PUT/DEL changed the usage -- and hands the numbers back with the
 * listing. A failed or old-server status just leaves quota_valid at 0.
 * Every status is one more round trip inside a listing, so not too
 * often: over a modem that is noticeable. */
#define QUOTA_INTERVAL 120
static long g_quota_time  = 0;
static int  g_quota_dirty = 1;

/* Thin wrappers binding libcloudcore's ac_*_datestamp() to this worker's
 * g_cfg -- the Amiga system clock carries no timezone info at all, so
 * converting to/from the server's UTC epoch needs gmt_offset/dst from
 * the loaded config -- something the handler's packet loop can never
 * have (it never reads any file, see handler.c), but the worker already
 * loaded via config_load() below. */
static LONG datestamp_to_epoch(const struct DateStamp *ds)
{
    return ac_datestamp_to_epoch(ds, g_cfg.gmt_offset, g_cfg.dst);
}

static void epoch_to_datestamp(LONG epoch, struct DateStamp *ds)
{
    ac_epoch_to_datestamp(epoch, g_cfg.gmt_offset, g_cfg.dst, ds);
}

/* Deferred network init (docs/design/handler-plan.md Stufe 2 "Boot-Reihenfolge-
 * Falle"): CreatePort() happens before this is ever called, so a Mount at
 * Activate=1/early boot doesn't hang waiting for Roadshow. Not cached as
 * a permanent failure -- retried on the next request that needs it.
 * Returns 1 once bsdsocket.library/AmiSSL are open and safe to call into,
 * 0 otherwise -- callers MUST check this before touching any cmd_*()
 * function: SocketBase/AmiSSLBase stay NULL on failure, and the vbcc
 * inline stubs for bsdsocket.library/AmiSSL do not NULL-check their own
 * library base, so a request attempted anyway would jump through a NULL
 * base pointer instead of failing gracefully. */
static int ensure_net_ready(void)
{
    if (g_net_ready) return 1;
    if (http_init()) {
        http_warmup(g_cfg.server, g_cfg.verify_ssl);
        g_net_ready = 1;
    }
    return g_net_ready;
}

/* ---- silent UI hooks (docs/design/handler-plan.md Stufe 2 "stille ui_hooks") */
/* The worker has no console (SYS_Input/Output NIL:, see backend.c) and
 * its only real "caller" is the handler, which only looks at
 * WorkerMsg.result -- so these all discard their input. Verbose HTTP
 * logging, if cfg.debug is set, goes through http_amiga.c's own
 * http_log_path mechanism instead (wired in main() below), not through
 * ui_notify(). */
static void     w_notify(int level, const char *msg)   { (void)level; (void)msg; }
static int      w_confirm(const char *msg)              { (void)msg; return 1; }
static int      w_progress(long done, long total, const char *name)
{
    (void)done; (void)total; (void)name;
    /* "Break <worker>" (Ctrl-C) cancels the running transfer -- the
     * only way to stop a long one, the handler itself never can. */
    return !ui_break_pending();
}
static UiOverwrite w_overwrite(int is_dir, const char *name,
                               long src_size, const char *src_date, const char *src_md5,
                               long dst_size, const char *dst_date, const char *dst_md5)
{
    (void)is_dir; (void)name; (void)src_size; (void)src_date; (void)src_md5;
    (void)dst_size; (void)dst_date; (void)dst_md5;
    return UI_OW_OVERWRITE; /* never actually consulted by the cmd_*() calls below */
}

/* A WREQ_PUT that failed would lose the data for good: Close() frees the
 * handle either way (actions.c act_end()). Keep a copy in
 * T:AmiCloud-unsaved/<name> instead, overwriting an older one of the
 * same name. Best effort -- nothing else to do if even that fails. */
static void save_unsaved(const char *path, const UBYTE *buf, ULONG len)
{
    const char *name = strrchr(path, '/');
    char target[300];
    BPTR lock, fh;

    name = name ? name + 1 : path;
    if (!*name) return;
    lock = CreateDir((CONST_STRPTR)"T:AmiCloud-unsaved");
    if (lock) UnLock(lock);
    snprintf(target, sizeof(target), "T:AmiCloud-unsaved/%s", name);
    fh = Open((CONST_STRPTR)target, MODE_NEWFILE);
    if (!fh) return;
    if (len) Write(fh, (APTR)buf, (LONG)len);
    Close(fh);
    ac_log_printf("worker: upload of %s failed, data kept in %s\n", path, target);
}

/* ---- HTTP status -> DOS error mapping (docs/design/handler-plan.md Stufe 2) -- */
/* cmd_get_buf()/cmd_put_buf() are the only two calls below that expose the
 * raw HTTP status through an explicit status_out parameter; every other
 * cmd_*() wrapper only returns 0/1 (unchanged from the plain CLI, see
 * cmd.h). For those, cmd_last_status() (libcloudhttp/cmd.c) is used
 * instead -- it tracks the status of the most recently completed request
 * regardless of which cmd_*() function made it, so a 401 (wrong
 * credentials) is no longer indistinguishable from a 404 (not found) for
 * list/del/mkdir/rename/info/setdate/setcomment/setprotect. */
static LONG map_get_status(int status)
{
    switch (status) {
    case 404: return ERROR_OBJECT_NOT_FOUND;
    case 401: return ERROR_DISK_NOT_VALIDATED;
    case 429: return ERROR_DISK_NOT_VALIDATED; /* login throttled, like 401 */
    case 403: return ERROR_WRITE_PROTECTED;
    case 0:   return ERROR_NO_DISK;       /* transport failure: server unreachable,
                                           * timed out or cancelled -- DOS offers
                                           * "Retry" for this one */
    default:  return ERROR_INVALID_LOCK;
    }
}

static LONG map_put_status(int status)
{
    switch (status) {
    case 404: return ERROR_OBJECT_NOT_FOUND;
    case 401: return ERROR_DISK_NOT_VALIDATED;
    case 429: return ERROR_DISK_NOT_VALIDATED; /* login throttled, like 401 */
    case 403: return ERROR_WRITE_PROTECTED;
    case 413: return ERROR_DISK_FULL;
    case 507: return ERROR_DISK_FULL;          /* quota exceeded */
    case 422: return ERROR_DISK_FULL;
    case 0:   return ERROR_NO_DISK;       /* transport failure, see map_get_status() */
    default:  return ERROR_INVALID_LOCK;
    }
}

static void dispatch(WorkerMsg *m)
{
    switch (m->cmd) {
    case WREQ_PING:
        m->result = 0;
        break;

    case WREQ_LIST: {
        AcEntry *entries = NULL;
        int n = 0, i;
        WEntry *out;

        if (!ensure_net_ready()) { m->result = map_get_status(0); break; }
        if (!cmd_list_raw(&g_cfg, m->path, &entries, &n)) {
            m->result = map_get_status(cmd_last_status());
            break;
        }
        m->entries = NULL;
        m->n_entries = 0;
        if (n > 0) {
            out = (WEntry *)AllocVec((ULONG)n * sizeof(WEntry), MEMF_PUBLIC | MEMF_CLEAR);
            if (!out) {
                cmd_entries_free(entries);
                m->result = ERROR_NO_FREE_STORE;
                break;
            }
            for (i = 0; i < n; i++) {
                strncpy(out[i].name, entries[i].name, sizeof(out[i].name) - 1);
                out[i].is_dir = entries[i].is_dir;
                out[i].size = entries[i].size;
                epoch_to_datestamp(entries[i].mtime, &out[i].mtime);
                out[i].protection = entries[i].protection;
                strncpy(out[i].comment, entries[i].comment, sizeof(out[i].comment) - 1);
                strncpy(out[i].md5, entries[i].md5, sizeof(out[i].md5) - 1);
            }
            m->entries = out;
            m->n_entries = n;
        }
        cmd_entries_free(entries);
        m->server_now = (LONG)time(NULL);
        m->result = 0;

        if (g_quota_dirty || m->server_now - g_quota_time >= QUOTA_INTERVAL) {
            AcStatus st;
            g_quota_time  = m->server_now;
            g_quota_dirty = 0;
            if (cmd_status_ex(&g_cfg, &st) && st.has_quota) {
                m->quota_used_kb  = st.quota_used_kb;
                m->quota_total_kb = st.quota_total_kb;
                m->quota_valid    = 1;
            }
        }
        break;
    }

    case WREQ_GET: {
        char *buf = NULL;
        long len = 0;
        int status = 0;

        if (!ensure_net_ready()) { m->result = map_get_status(0); break; }
        /* cmd_get_buf()/cmd_put_buf() return 0 on success, 1 on failure --
         * the OPPOSITE convention from cmd_list_raw()/cmd_info_raw() (1 on
         * success, 0 on failure) used elsewhere in this switch. This was
         * inverted (checked with "!cmd_get_buf(...)"), so a SUCCESSFUL
         * download always took the failure branch: it reported
         * ERROR_INVALID_LOCK (map_get_status(200) falls to the default
         * case) and leaked the just-AllocVec'd buffer, since m->buf/
         * m->buflen were never reached. Every WREQ_GET was silently
         * mis-reported this way. */
        {
            int rc = cmd_get_buf(&g_cfg, m->path, &buf, &len, &status);
            ac_log_printf("dispatch: WREQ_GET cmd_get_buf returned %d (status=%d)\n", rc, status);
            if (rc != 0) {
                m->result = map_get_status(status);
                ac_log_printf("dispatch: WREQ_GET failure branch, m->result=%ld, breaking\n", (long)m->result);
                break;
            }
        }
        ac_log_printf("dispatch: WREQ_GET success branch, setting m->buf=%p m->buflen=%ld\n", (void *)buf, len);
        m->buf = (UBYTE *)buf;
        m->buflen = (ULONG)len;
        m->result = 0;
        ac_log_printf("dispatch: WREQ_GET fields set, breaking\n");
        break;
    }

    case WREQ_PUT: {
        int status = 0;

        if (!ensure_net_ready()) {
            m->result = map_put_status(0);
            save_unsaved(m->path, m->buf, m->buflen);
            break;
        }
        /* Same inverted-check bug as WREQ_GET above -- cmd_put_buf() also
         * returns 0 on success, 1 on failure. */
        if (cmd_put_buf(&g_cfg, m->path, (const char *)m->buf, (long)m->buflen, &status) != 0) {
            m->result = map_put_status(status);
            save_unsaved(m->path, m->buf, m->buflen);
            break;
        }
        m->result = 0;
        g_quota_dirty = 1;
        break;
    }

    case WREQ_DEL:
        if (!ensure_net_ready()) { m->result = map_put_status(0); break; }
        m->result = cmd_del(&g_cfg, m->path) ? map_put_status(cmd_last_status()) : 0;
        if (m->result == 0) g_quota_dirty = 1;
        break;

    case WREQ_MKDIR:
        if (!ensure_net_ready()) { m->result = map_put_status(0); break; }
        m->result = cmd_mkdir_quiet(&g_cfg, m->path) ? map_put_status(cmd_last_status()) : 0;
        break;

    case WREQ_RENAME:
        if (!ensure_net_ready()) { m->result = map_put_status(0); break; }
        m->result = cmd_rename(&g_cfg, m->path, m->path2) ? map_put_status(cmd_last_status()) : 0;
        break;

    case WREQ_INFO: {
        AcInfo info;

        if (!ensure_net_ready()) { m->result = map_get_status(0); break; }
        if (cmd_info_raw(&g_cfg, m->path, &info) != 0) {
            m->result = map_get_status(cmd_last_status());
            break;
        }
        epoch_to_datestamp(info.mtime, &m->mtime);
        m->protection = info.protection;
        strncpy(m->comment, info.comment, sizeof(m->comment) - 1);
        m->result = 0;
        break;
    }

    case WREQ_SETDATE:
        if (!ensure_net_ready()) { m->result = map_put_status(0); break; }
        m->result = cmd_setdate_mtime(&g_cfg, m->path, datestamp_to_epoch(&m->mtime)) ? map_put_status(cmd_last_status()) : 0;
        break;

    case WREQ_SETCOMMENT:
        if (!ensure_net_ready()) { m->result = map_put_status(0); break; }
        m->result = cmd_setcomment(&g_cfg, m->path, m->comment) ? map_put_status(cmd_last_status()) : 0;
        break;

    case WREQ_SETPROTECT:
        if (!ensure_net_ready()) { m->result = map_put_status(0); break; }
        m->result = cmd_setprotect(&g_cfg, m->path, m->protection) ? map_put_status(cmd_last_status()) : 0;
        break;

    default:
        m->result = ERROR_ACTION_NOT_KNOWN;
        break;
    }
}

int main(int argc, char **argv)
{
    struct MsgPort *port;
    int running = 1;

    (void)argc; (void)argv;

    config_load("ENV:amicloud.cfg", &g_cfg);

    {
        UiHooks h;
        h.notify    = w_notify;
        h.confirm   = w_confirm;
        h.progress  = w_progress;
        h.overwrite = w_overwrite;
        ui_set_hooks(&h);
    }
    ac_errors_to_stderr = 0;
    if (g_cfg.debug[0]) {
        http_log_bptr = 1;
        strncpy(http_log_path, g_cfg.debug, sizeof(http_log_path) - 1);
        http_log_path[sizeof(http_log_path) - 1] = '\0';
    }

    ac_log_printf("main: sizeof(WorkerMsg)=%ld sizeof(WEntry)=%ld offsetof(buf)=%ld offsetof(result)=%ld\n",
                  (long)sizeof(WorkerMsg), (long)sizeof(WEntry),
                  (long)((char *)&((WorkerMsg *)0)->buf - (char *)0),
                  (long)((char *)&((WorkerMsg *)0)->result - (char *)0));

    port = CreateMsgPort();
    if (!port) return RETURN_FAIL;
    port->mp_Node.ln_Name = (char *)AC_WORKER_PORT;
    port->mp_Node.ln_Pri  = 0;
    AddPort(port);

    while (running) {
        struct Message *raw;
        WorkerMsg *m;

        WaitPort(port);
        while ((raw = GetMsg(port)) != NULL) {
            m = (WorkerMsg *)raw;
            if (m->cmd == WREQ_QUIT) {
                m->result = 0;
                ReplyMsg((struct Message *)m);
                running = 0;
                continue;
            }
            ac_log_printf("main: dispatching cmd=%d m=%p mn_ReplyPort=%p\n",
                          (int)m->cmd, (void *)m, (void *)m->msg.mn_ReplyPort);
            dispatch(m);
            /* Every reply carries the clock and the cache lifetime, the
             * handler has neither (see proto.h). */
            m->server_now = (LONG)time(NULL);
            m->cache_ttl  = g_cfg.cache_ttl;
            /* A Break only cancels the request it hit, not every later one. */
            ui_break_clear();
            ac_log_printf("main: dispatch returned, mn_ReplyPort=%p, calling ReplyMsg...\n",
                          (void *)m->msg.mn_ReplyPort);
            ReplyMsg((struct Message *)m);
            ac_log_printf("main: ReplyMsg done\n");
        }
    }

    RemPort(port);
    DeleteMsgPort(port);
    if (g_net_ready) http_cleanup();
    return RETURN_OK;
}

#endif /* PLATFORM_AMIGA */
