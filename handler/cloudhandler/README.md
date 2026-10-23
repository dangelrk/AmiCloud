# CLOUD: -- AmiCloud DOS Handler

> Part of **AmiCloud** (AmigaOS only). End-user documentation: the
> manual (`docs/user/AmiCloud.guide`, `docs/user/AmiCloud-Handbuch.html`),
> installation: `docs/INSTALL.md`, build: `docs/BUILD.md`. This file is
> the developer view of the handler. `worker.c` links `libcloudhttp`/
> `libcloudcore` from the AmiCloudCore submodule (`../../core/`); the
> design history is in `../../docs/design/handler-plan.md` (historical
> planning document, paths there refer to the old monorepo).

An AmigaOS 3.x DOS handler that makes AmiCloud storage available as a
regular device, `CLOUD:` (plus a `AmiCloud:` volume/Workbench icon
alongside it). Companion to the `amicloud` CLI, `amicloud-shell` and the
Commander in `../../apps/` -- see `../../docs/design/handler-plan.md` for
the full three-stage design this code follows.

**Stage 1: RAM-only proof of concept.** Everything lived in a RAM tree
inside the handler process, no server connection, no persistence across a
remount. Got the DOS-handler plumbing itself right -- packet parsing,
BCPL string handling, locks, file handles, directory scanning -- before
adding a second process and a network backend on top of it.

**Stage 2: real server backend via a worker process.** `vol.c`'s RAM tree
became a *cache* of server state instead of the only copy. A second
process, `amicloud-worker` (`worker.c`), does all the actual HTTP/TLS
work -- the handler itself stays exec.library-only for its entire
lifetime (see "Two-process discipline" below) and can never touch
bsdsocket/AmiSSL directly.

**Stage 3: full metadata support.** `ACTION_SET_PROTECT`/
`ACTION_SET_COMMENT`/`ACTION_SET_DATE` now reach the server too
(`WREQ_SETPROTECT`/`SETCOMMENT`/`SETDATE`), libcloudhttp's `cmd.c` gained
`cmd_setcomment()`/`cmd_setprotect()` for this, and directory listings
carry real protection bits and comments (`AcEntry` extended, parsed in
`cmd_list_raw()`). Dates are now converted with the actual `gmt_offset`/
`dst` from the loaded config, done in the worker (the only place that
config is available) rather than the rough UTC-only approximation Stage 2
used -- via libcloudcore's `ac_datestamp_to_epoch()`/
`ac_epoch_to_datestamp()` (the one shared implementation, see M6 note
above). This is the plan's final stage -- see `../../docs/design/handler-plan.md` for
what was deliberately left out (`sync`, multi-volume, disk-swap, etc.).

## Why `-nostdlib`

`handler.c` is linked without vc's normal startup code or `vc.lib`. Two
reasons:

1. A DOS handler process's entry point is dictated by the mountlist's
   `GlobVec` field, not by a `main()` convention. With `GlobVec = -1` (a
   plain C/assembler handler, no BCPL), AmigaOS starts the process by
   jumping to the very first byte of the loaded executable (RKRM DOS
   11.5.4) -- there is no `main()` call, no `argc`/`argv`, and critically
   no guarantee about what's in any CPU register at that point. vc's
   normal crt0 assumes it *is* that first byte and sets up a `main()` call
   convention we don't want; `-nostdlib` removes it so `handler.c`'s own
   `_start()` (declared `__saveds` -- it has to install its own global-data
   register, nothing else will) is what actually ends up first, as long as
   `handler.o` stays first on the link line (see `Makefile.amiga-cross`).
2. Once the handler has replied its startup packet and entered the packet
   loop, it must never call anything in `dos.library` again -- see the
   "two-process discipline" comment at the top of `handler.c` and the
   background below. Keeping stdio/malloc out entirely (own `str*`/`mem*`
   in `util.c`, `AllocVec`/`FreeVec` instead of `malloc`/`free`) removes an
   entire category of "did that call quietly pull in something that talks
   to dos.library" mistakes.

## Two-process discipline (why this handler is careful)

An earlier version of this handler (deleted, see `../../docs/design/handler-plan.md`
"Entscheidende Vorerfahrung") crashed AmigaOS with a Guru `0x87000004`
(`AN_DOSLib+4`) because its single process's `pr_MsgPort` was simultaneously
the DOS packet port *and* something dos.library itself needed while
replying/servicing a request through it -- the same trap `http_warmup()`'s
own doc comment in libcloudhttp's `http_amiga.c` describes in detail. The fix,
and the reason this is a *two*-process design from Stage 2 on: the
handler process (`handler.c`) never links or calls `http_amiga.c` at all,
and follows the rule that avoided that crash regardless: **`dos.library`
is only opened, used, and closed during straight-line startup code,
before the packet loop is entered.** From `ACTION_STARTUP` onward once the
loop starts, `handler.c`/`actions.c`/`vol.c`/`backend.c` touch only
`exec.library` (`WaitPort`/`GetMsg`/`PutMsg`/`AllocVec`/`FreeVec`/
`CopyMem`/`Forbid`/`FindPort`, ...), and packets are replied with a raw
`PutMsg()` to `pkt->dp_Port`/`pkt->dp_Link` -- never `ReplyPkt()`,
`DoPkt()`, or `Delay()` from inside the loop. All actual network I/O runs
in the second process, `amicloud-worker`, which has none of these
restrictions (full vbcc startup, full libc, its own `pr_MsgPort` never
doubles as a DOS device port) -- see "Architecture" below.

One consequence worth calling out explicitly: `docs/design/handler-plan.md`'s
Stage 1 section lists `MakeDosEntry`+`AddDosEntry` (the calls that create
the `AmiCloud:` volume/Workbench icon) as happening *before* the startup
packet is replied. Cross-checking against RKRM DOS 8.4.1 and 13.1.2 turned
up a real deadlock risk in that ordering: `GetDeviceProc()` holds the
device list locked for the *entire* startup handshake, right up until we
reply the startup packet, and `AddDosEntry()` itself needs to lock that
same list. Calling it before replying would mean waiting on a lock held by
someone who is themselves waiting for our reply -- self-deadlock. This
implementation therefore replies the startup packet first and only then
calls `MakeDosEntry`/`AddDosEntry` (guarded by `AttemptLockDosList()`, as
RKRM recommends for exactly this situation) -- see `register_volume()` in
`handler.c`. Functionally this makes no difference (the icon still
appears once mounting completes); it just does so without a chance of
hanging the whole machine.

## Architecture

```
   AmigaDOS  <--packets-->  handler.c/actions.c/vol.c   <--WorkerMsg-->   worker.c
  (any app)                 (exec.library only, no                    (full vbcc startup,
                             libc, own entry point,                    links libcloudhttp's
                             -nostdlib)                                 cmd.c/http_amiga.c/...)
                                                                              |
                                                                          HTTPS/AmiSSL
                                                                              v
                                                                       AmiCloud server
```

- **`proto.h`** -- the wire protocol between the two processes: `WReqCmd`
  (`WREQ_LIST`/`GET`/`PUT`/`DEL`/`MKDIR`/`RENAME`/`INFO`/`SETDATE`/
  `SETCOMMENT`/`SETPROTECT`/...) and `WorkerMsg`, a plain `exec.library`
  `Message` the handler `PutMsg()`s to the worker's well-known port
  (`AC_WORKER_PORT`, found via `FindPort()`) and the worker `ReplyMsg()`s
  back. v1 is synchronous: `backend_send()` blocks the whole handler for
  one round trip (accepted for now, see `proto.h`). Dates on the wire
  (`WEntry.mtime`, `WorkerMsg.mtime`) are a `struct DateStamp`, not a raw
  epoch -- the worker converts in both directions (thin wrappers in
  `worker.c` around libcloudcore's `ac_epoch_to_datestamp()`/
  `ac_datestamp_to_epoch()` -- the one shared implementation, module-
  encapsulation rule) since only it has the config's `gmt_offset`/`dst`;
  the handler just copies the result straight into a `CNode`/`fib_Date`.
- **`backend.c`/`.h`** -- the handler side: `backend_init()` (startup-only,
  needs `dos.library`) finds or spawns the worker; `backend_send()` (packet
  loop, `exec.library` only) does the synchronous request/reply.
- **`worker.c`** -- a normal AmigaOS process. Links libcloudhttp's `cmd.c`
  and friends unchanged (`cmd_list_raw`, `cmd_del`, `cmd_mkdir_quiet`,
  `cmd_rename`, `cmd_info_raw`, `cmd_setdate_mtime`), plus the helpers
  added to `cmd.c` for this handler: `cmd_get_buf`/`cmd_put_buf`
  (Stage 2 -- Stage 1's `cmd_get`/`cmd_put` round-trip through a local
  file, which the worker has no use for) and `cmd_setcomment`/
  `cmd_setprotect` (Stage 3).
- **Directory listing cache** (`vol.c`, `CNode.listed`/`cache_time`,
  `CACHE_TTL` = 30s, overridden by `cache_ttl=` in `amicloud.cfg`):
  `vol_find_child()` transparently refreshes a stale directory via
  `WREQ_LIST` before searching it, so every existing path-walking
  function in `actions.c` became cache-aware for free. "Now", for TTL
  purposes, comes from the worker's clock in every reply
  (`WorkerMsg.server_now`, `backend_now()`), the configured TTL likewise
  (`cache_ttl`) -- the packet loop has no clock or file access of its
  own. If a directory that was never listed can't be listed (server
  unreachable), lookups and `Examine` fail with that error instead of
  presenting it as empty. A locked/exclusively-locked/open child always
  survives a refresh untouched, even if the server no longer lists it.
  Mutations (`MKDIR`/`DEL`/`RENAME`/`PUT`-via-`ACTION_END`) apply directly
  to the tree *and* invalidate the parent's cache (`vol_invalidate()`), so
  the next listing re-verifies against the server rather than trusting the
  optimistic local update forever.
- **File content lives per open handle, not on the `CNode`** (`OpenFile.
  data`/`size`/`alloc`, `actions.h`): `FINDINPUT`/`FINDUPDATE`/
  `FH_FROM_LOCK` on an existing file fetch the whole thing via `WREQ_GET`
  when opened; `WRITE`/`SET_FILE_SIZE` mutate that buffer and set
  `dirty`; `ACTION_END` uploads it via `WREQ_PUT` iff `dirty`. A failed
  upload still frees the handle and releases the file (dos.library's
  `Close()` frees the `FileHandle` either way and hardly any program
  retries -- keeping it only leaked the buffer and left the file locked
  until the next mount); the worker saves the data to
  `T:AmiCloud-unsaved/<name>` first.
- **Buffer ownership always crosses the process boundary as `AllocVec`/
  `FreeVec`, one owner per direction** (both processes share one flat
  address space -- classic AmigaOS, no IPC copying needed, see `proto.h`):
  the handler `AllocVec`s a `WREQ_PUT` body and `FreeVec`s it itself after
  the reply; the worker `AllocVec`s a `WREQ_GET` body or `WREQ_LIST`
  entries array and the handler `FreeVec`s it after consuming it.

## Known limitations

These are deliberate scope cuts across all three stages, not bugs to fix
later within this plan (see `../../docs/design/handler-plan.md` for what was
intentionally left for a future plan, e.g. `sync`, multi-volume/disk-swap
handling):

- One request to the worker at a time (see `proto.h`). While it runs,
  `backend_send()` keeps answering packets that need no network
  (`handler.c` `serve_while_busy()`: Info, Read/Write/Seek on open files,
  Examine of a fresh directory, FreeLock, ...) and queues the rest, which
  run in arrival order afterwards. So a long transfer over a slow line no
  longer freezes Workbench's view of the volume, but a second transfer
  still waits for the first.
- No handler-side timeout: the worker's HTTP layer has idle timeouts
  (`timeout=` in `amicloud.cfg`, default 120 s -- fails only when no data
  moves at all, never limits a whole transfer), so a reply always comes.
  A transport failure (unreachable, timed out, broken off) is reported as
  `ERROR_NO_DISK`, for which DOS offers "Retry"; a broken-off download is
  resumed by the HTTP layer itself first. `Break` on `amicloud-worker`
  cancels the running transfer. An earlier ~60 s `timer.device` bound
  here was removed: at modem speed a legitimate 200 KB read takes longer,
  and it killed the mount for good. If something hangs anyway,
  `debug=T:worker.log` in `ENV:amicloud.cfg` turns on the worker's
  verbose HTTP/TLS trace (`http_amiga.c`'s `HLOG`/`HERR`).
- `ACTION_DIE` always fails (`ERROR_OBJECT_IN_USE`): a clean unmount needs
  `dos.library` (`AttemptLockDosList`/`RemDosEntry`), which is closed by
  the time the packet loop is servicing requests. Unmounting means
  rebooting, same as many minimal third-party handlers. The worker process
  is also never told to quit (`WREQ_QUIT` exists in the protocol but
  nothing sends it yet) -- kill it manually (`Break <pid>` /
  `run >NIL: L:amicloud-worker` lets you see its CLI process) if needed.
- Diagnostics: `Type CLOUD:debuglog` shows the handler-side trace of its
  requests to the worker (`diag_log()` ring buffer in `backend.c`, 4 KB,
  stops recording when full). This synthetic file is intercepted before
  any server lookup, so a real file named `debuglog` in the cloud's root
  directory is not reachable through `CLOUD:` (use the CLI instead).
- `ACTION_EXAMINE_ALL` is intentionally unimplemented; DOS transparently
  falls back to `ACTION_EXAMINE_OBJECT`/`ACTION_EXAMINE_NEXT`.
- `DISK_INFO`/`INFO` report a fixed, plausible-but-not-real capacity
  (512-byte blocks, ~2GB, "half full") -- this was always the plan's
  target for these fields (docs/design/handler-plan.md Stufe 3 "Restpunkte"),
  not a placeholder waiting on a later stage; a real usage figure would
  need a recursive size query the server doesn't currently expose cheaply.
- No recursive delete in the handler: `ACTION_DELETE_OBJECT` on a
  non-empty directory always fails with `ERROR_DIRECTORY_NOT_EMPTY`, by
  design -- `Delete ... ALL` from Shell already handles recursion itself
  via repeated `ExNext`/`DELETE_OBJECT`, same as any other AmigaDOS
  filesystem.
- Single volume, single handler process, no disk-swap/multi-volume
  handling (`ACTION_INHIBIT`, `ACTION_RENAME_DISK`, `ACTION_FORMAT` etc.
  are all unimplemented -- fall through to `ERROR_ACTION_NOT_KNOWN`).
- Error mapping from HTTP status to DOS error codes is precise for
  `WREQ_GET`/`WREQ_PUT` (the two operations it matters most for) but
  coarse for `LIST`/`DEL`/`MKDIR`/`RENAME`/`INFO`/`SETDATE`/`SETCOMMENT`/
  `SETPROTECT`, since the reused `cmd_*()` wrappers from libcloudhttp's
  `cmd.c` only return success/failure, not the HTTP status -- see
  `worker.c`'s mapping-table comment.

## Build

Cross-compiled from a host with vbcc (the verified way), either as part of
the whole project or on its own:

```
export VBCC=/path/to/vbcc          # layout: see docs/BUILD.md
make amiga                         # top level, all Amiga programs
make -C handler/cloudhandler -f Makefile.amiga-cross   # just this handler
```

Produces `bin/amiga/amicloud-handler` and `bin/amiga/amicloud-worker`.
Natively on the Amiga, `Execute Build-Amiga` in the project root builds
them together with everything else (generated from `Makefile.amiga-cross`,
see `docs/BUILD.md`; not yet tested on real hardware).

## Install

`make dist-amiga` puts both binaries into the installation drawer's `C/`
and this directory's `mount/CLOUD` into `Storage/DOSDrivers/`;
`Install-AmiCloud` copies them to their final places. By hand:

```
Copy amicloud-handler L:
Copy amicloud-worker  L:
Copy mount/CLOUD DEVS:DOSDrivers/
```

`amicloud-worker` must be `L:amicloud-worker` (`backend_init()` spawns it
from exactly there). It needs the same config the CLI/shell use --
`ENV:amicloud.cfg` (see `../../config-examples/amicloud.cfg.example` and
`../../config-examples/README.md`, editor: AmiCloudPrefs) -- with
`server`/`user`/`pass` set; without it, the worker still starts and
answers `WREQ_PING`, but every real request fails with a network/auth
error until one is in place. The worker reads the file once at startup,
so later changes only take effect after a reboot.

Then either reboot, or mount it immediately from a Shell:

```
Mount CLOUD:
```

`backend_init()` first checks for an already-running worker
(`FindPort("AmiCloud-Worker")`); if none is found it spawns
`L:amicloud-worker` itself and waits up to ~30s for it to register its
port. Alternatively, start it yourself first (e.g. `run >NIL:
L:amicloud-worker` from `S:User-Startup`, before the `Mount`) if you want
it to outlive individual mount/unmount cycles or want its own visible CLI
process to `Break` later.

If mounting fails, the handler process reports the failure back through
`Mount`'s usual error requester/return code -- either `ACTION_STARTUP`
failed locally (out of memory for the tree, `dos.library` itself
unavailable) or the worker never became reachable in time
(`ERROR_DEVICE_NOT_MOUNTED`, e.g. `amicloud-worker` isn't in `L:`).

## Test (Stage 3, real Amiga, needs a running AmiCloud server)

Run the Stage 1 and Stage 2 batteries below first, then:

1. `Protect CLOUD:test1 +p`, then check via `web.php`/the CLI
   (`amicloud info /test1`) that the bits actually landed on the server,
   not just the local cache -- `List CLOUD:test1` alone isn't enough
   proof, it might just be echoing what the handler wrote locally.
2. `FileNote CLOUD:test1 "hello"`, same server-side check. Also try a
   comment with a space and a special character (`&`, `%`) in it -- the
   client now URL-encodes it (`cmd_setcomment()`), confirm it round-trips
   intact rather than getting truncated at the first space.
3. `SetDate CLOUD:test1 today` (or any AmigaDOS date-setting tool), then
   remount `CLOUD:` (or wait past the TTL and `Dir` again) and confirm the
   date read back matches -- this exercises the full round trip:
   `DateStamp` -> worker's `datestamp_to_epoch()` -> server -> next
   `WREQ_LIST`'s epoch -> worker's `epoch_to_datestamp()` -> `DateStamp`
   again. A mismatch here most likely means `ENV:amicloud.cfg`'s
   `gmt_offset`/`dst` don't match the server's actual timezone (or each
   other) -- check both sides agree before assuming it's a bug.
4. Comment >79 characters: `FileNote` with a long string should fail
   locally with `ERROR_COMMENT_TOO_BIG` *before* any network request (the
   hard cap is enforced in `act_set_comment()`, not the server).
5. `Rename CLOUD:Test CLOUD:test` (case-only, same object) -- confirm
   whether the server actually renames it or treats it as a no-op; either
   is acceptable, but if it's a no-op, that's worth remembering as a
   platform quirk rather than re-litigating as a bug later.
6. `Copy` a large-ish file in with `FINDUPDATE`-style tools (or manually:
   open for update, truncate shorter via whatever truncates on your
   testing tool, close) -- confirm the *shorter* content is what actually
   lands on the server, not the original longer one.
7. `Info CLOUD:` / the Workbench window's title bar -- capacity/"% full"
   numbers should be plausible (not garbage/negative from 32-bit overflow)
   even though they're not derived from real usage (see "Known
   limitations").

## Test (Stage 2, real Amiga, needs a running AmiCloud server)

First repeat the full Stage 1 battery below against the real server (same
steps, now actually persisted) to catch anything the cache layer broke,
then add:

1. `Dir CLOUD:` -- shows the server's actual root contents (first access
   after mount always triggers a real `WREQ_LIST`, cache starts empty).
2. `List CLOUD:` twice within 30 seconds (`cache_ttl`) -- second one
   should be noticeably instant (served from cache, no `WREQ_LIST` round
   trip); wait 30+ seconds and repeat -- should trigger a fresh fetch.
3. Copy a real file in (`Copy S:Startup-Sequence CLOUD:test1`), verify via
   `web.php` or the CLI (`amicloud get /test1 -`) that it actually landed
   on the server with matching content.
4. From a second machine/session, upload or delete something in the same
   directory the Amiga has open, then `Dir` again after the TTL -- the
   change should show up (cache re-validates against the server, doesn't
   just trust its own view forever).
5. Open a file for read (`Type CLOUD:test1`) while it's also open for
   write elsewhere (or vice versa) -- confirm the locked/open node is
   never silently replaced or dropped out from under you mid-scan.
6. Unplug the network (or point `server` at something unreachable) mid-
   `Copy` -- the copy should fail with a clear DOS error, the handler
   process must **not** crash, and a `Dir CLOUD:` afterwards (network
   restored) should recover normally.
7. Mount without `amicloud-worker` present in `L:` -- `Mount CLOUD:`
   should fail cleanly (error requester, no hang, no crash) rather than
   wait forever.
8. Restart just the worker (kill it, `run >NIL: L:amicloud-worker` again)
   without unmounting `CLOUD:` -- `backend_init()`'s `FindPort()` retry
   only runs at mount time, so a request made *after* an unexpected worker
   death is expected to fail (documented limitation, see above); confirm
   it fails cleanly rather than hanging, and that a fresh `Mount` (or a
   future crash-recovery feature) is the way to get going again.

### Test (Stage 1 battery, still required first, no network needed)

1. `Dir CLOUD:` -- lists an empty root.
2. `MakeDir CLOUD:Test`
3. `Copy S:Startup-Sequence CLOUD:Test/` then `Type CLOUD:Test/Startup-Sequence`
   -- content matches the original.
4. Copy it back out to `RAM:` and compare (`Copy CLOUD:Test/Startup-Sequence RAM:` then diff/compare against the original).
5. `Rename CLOUD:Test/Startup-Sequence CLOUD:Test/SS`
6. `Protect CLOUD:Test/SS +p` then `List CLOUD:Test` -- protection bits show.
7. `FileNote CLOUD:Test/SS "a comment"` then `List CLOUD:Test` -- comment shows.
8. `Delete CLOUD:Test/SS`, then `Delete CLOUD:Test` (now empty) -- both succeed;
   `Delete CLOUD:Test` while non-empty should instead fail with "directory not empty".
9. Open a Workbench window on `AmiCloud:` -- icon appears, drag&drop works,
   "New Drawer" works.
10. Stress: `Copy SYS:Utilities CLOUD: ALL` (many files/dirs at once).
11. While that copy is running, open a second Shell and run `Dir CLOUD:`
    repeatedly -- directory scanning must stay stable (no duplicate/missing
    entries, no crash) while the tree is being modified concurrently
    (`ACTION_EXAMINE_NEXT`'s cursor is a monotonic per-node sequence
    number specifically so this is safe, see `vol.h`/`actions.c`).

## Layout

- `handler.c` -- entry point, `ACTION_STARTUP`, packet dispatch loop.
- `actions.c`/`.h` -- one function per `ACTION_*`.
- `vol.c`/`.h` -- the RAM tree/cache (`CNode`), locks, growth allocator,
  directory listing TTL cache.
- `util.c`/`.h` -- mini `str`/`mem` routines, BCPL string helpers, path
  tokenizer (no libc, see "Why `-nostdlib`" above).
- `proto.h` -- handler <-> worker wire protocol.
- `backend.c`/`.h` -- handler-side worker client (spawn/find, synchronous
  send/reply).
- `worker.c` -- the `amicloud-worker` process (normal vbcc startup, links
  libcloudhttp/libcloudcore).
- `Makefile.amiga-cross`, `mount/CLOUD` -- build and DOSDriver
  (same values as a classic mountlist entry: `../../config-examples/CLOUD.mountlist`).
