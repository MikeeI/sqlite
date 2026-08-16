# ISSUE-005 — win: timed SHM lock skips completion collection

State: Hold
Mode: Undecided
Target: Undecided
Location: Not published.
Priority: High
Confidence: Medium
Type: reliability
Created: 2026-08-16
Updated: 2026-08-16
Source: `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`

## Root

[S] Under `SQLITE_ENABLE_SETLK_TIMEOUT`, `winHandleLockTimeout()` starts an overlapped `LockFileEx()` request, treats `WAIT_OBJECT_0` as `ret=TRUE` without collecting its completion result, then calls `CancelIo()` and closes `ovlp.hEvent` on every `ERROR_IO_PENDING` path (`src/os_win.c:1644-1707`; https://sqlite.org/src/raw/src/os_win.c?ci=db05b203f47e6ed0792ac011b18e0da98275e900407436798ca28d5f7dd2e094).

[S] Its one lifecycle root is incomplete ownership of the pending operation: event completion is conflated with lock success, while a timeout cancellation is requested but not drained before the stack `OVERLAPPED` and its event are torn down.

## Reach and impact

[S] `winHandleOpen()` adds `FILE_FLAG_OVERLAPPED` only for this feature's `*-shm` handles (`src/os_win.c:3150-3196`); `winShmMap()` reaches the DMS helper through `winLockSharedMemory()` (`3586-3613`), and `winShmLock()` reaches the same helper for WAL lock bytes with `winFileBusyTimeout(pDbFd)` (`3404-3549`).

[S] After a timeout, `winHandleLockTimeout()` retains `SQLITE_BUSY_TIMEOUT` for `SQLITE_ENABLE_SETLK_TIMEOUT==1`, or `SQLITE_BUSY` otherwise, while `winShmLock()` updates `sharedMask` or `exclMask` only for `SQLITE_OK` (`1681-1707`, `3529-3538`); if the race's terminal result is a granted lock, OS ownership can diverge from SQLite's masks, and the returned busy result can misdescribe that state.

[S] The DMS path has the same state boundary: `winLockSharedMemory()` changes `isUnlocked` only after an `SQLITE_OK` shared-lock result (`3115-3145`), so an uncollected post-timeout grant can also leave DMS bookkeeping stale.

[S] `CancelIo()` affects only pending I/O issued by its calling thread on the handle; this helper calls it on a handle that may be the node's shared UNC lock handle, so a correction must preserve the handle/thread ownership boundary rather than assume operation-local cancellation (https://learn.microsoft.com/en-us/windows/win32/fileio/cancelio; `src/os_win.c:3309-3313`, `3507-3530`).

[N] No Windows reproduction measures frequency, a leaked lock, an execution failure, or corruption; this record does not claim any observed database corruption or user-visible failure.

## Evidence

[S] The pinned Git mirror commit maps to canonical Fossil check-in `db05b203f47e6ed0792ac011b18e0da98275e900407436798ca28d5f7dd2e094`; its current `src/os_win.c` contains the cited control flow (https://sqlite.org/src/info/db05b203f47e6ed0792ac011b18e0da98275e900407436798ca28d5f7dd2e094).

[S] Microsoft documents that an `OVERLAPPED.hEvent` is signaled when its operation completes, while `GetOverlappedResult()` retrieves the pending operation's result; an event wait alone does not collect that result (https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-overlapped; https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult).

[S] Microsoft documents `CancelIo()` as a cancellation request whose operations still complete normally, says the caller cannot know whether completion succeeded or was canceled from handle state, and directs `GetOverlappedResult()` to determine completion (https://learn.microsoft.com/en-us/windows/win32/fileio/cancelio).

[S] Microsoft warns not to reuse an `OVERLAPPED` before its associated asynchronous operation completes; this helper's automatic `ovlp` leaves scope and its event closes at `1701-1702`, so a cancellation request that remains pending crosses the documented lifetime boundary (https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-overlapped).

[S] `LockFileEx()` accepts an `OVERLAPPED` and may return `ERROR_IO_PENDING` for asynchronous handles; its documentation names `GetOverlappedResult()` or a wait function for completion handling (https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex).

## Prior art

[S] Strongest prior art: canonical Fossil check-in `35b3e73c5a9efa12f9bb0dad1721fce128cd1e3bcbc87027ee4ea685a12a70d5` (2024-12-24, `win32-enable-setlk`) added the current pending-wait/cancel shape under the title “Properly wait for asynchronous results”; it is related, not a resolution, because current code still neither collects the result nor drains cancellation (https://sqlite.org/src/info/35b3e73c5a9efa12f9bb0dad1721fce128cd1e3bcbc87027ee4ea685a12a70d5; https://github.com/sqlite/sqlite/commit/ce50282c3bf662c1e12d066bff869c53532a3d74).

[S] Later canonical-origin changes for `SQLITE_ENABLE_SETLK_TIMEOUT=2` and recovery blocking changed timeout classification and eligible lock ordering, not this completion path (`8efb95e0d4670b9c5dbd8cf34512334f47951a8dff8fdadc8645f75076acd91f`; `8ac4525a2e3100bb5b9460cc49bb64f007911180d5f51461282b1de0201328b6`).

[O] On 2026-08-16, GitHub mirror searches for `LockFileEx` and `SQLITE_ENABLE_SETLK_TIMEOUT` returned no issues or pull requests; commit search found only the 2024-12-24 related change above.

[O] Forum searches for `CancelIo`, `GetOverlappedResult`, `ERROR_IO_PENDING`, `LockFileEx`, and `SQLITE_ENABLE_SETLK_TIMEOUT` found no thread owning this completion/cancellation root; the related 2026 timeout thread confirms WAL timeout scope but not this lifecycle (https://sqlite.org/forum/forumpost/eed46f6449).

## Direction

[A] Make `winHandleLockTimeout()` own one complete overlapped-request lifecycle: after `ERROR_IO_PENDING`, collect the final status with `GetOverlappedResult()` after a signal, and after a timeout request cancellation then drain terminal completion before closing the event or returning.

[A] Derive SQLite status only from the collected terminal result: preserve busy semantics for a completed cancellation, classify a terminal I/O failure through the existing lock error path, and return success if the race actually granted the lock so `winShmLock()` and `winLockSharedMemory()` update their ownership state.

[S] `aSyscall[]` makes `LockFileEx`, feature-gated `CreateEvent`, and `CancelIo` overrideable but has no `GetOverlappedResult` entry; adding one would require its construction assertions to remain correct (`src/os_win.c:373-711`, `730-793`, `5308-5318`).

## Bounds

[S] Scope is Windows WAL shared-memory locking compiled with `SQLITE_ENABLE_SETLK_TIMEOUT`; non-feature builds do not open these handles with `FILE_FLAG_OVERLAPPED`, and Unix VFS, ordinary database-file locks, source changes, index edits, and test changes are excluded.

[S] Preserve existing immediate lock failures, `nMs==0` pending-wait behavior, `SQLITE_ENABLE_SETLK_TIMEOUT` timeout-code variants, shared-UNC lock-handle behavior, and SQLite's rule that masks change only after a confirmed successful OS lock.

[A] Do not replace `CancelIo()` with a broader API without resolving compatibility and the shared-handle/thread cancellation contract; the correction need only make the current request's terminal state authoritative before teardown.

## Verification

Test decision: none.

[A] A focused Windows-only two-process WAL contention reproduction built with `SQLITE_ENABLE_SETLK_TIMEOUT=1` should force `ERROR_IO_PENDING`, a finite timeout, and the timeout-to-grant race while tracing `LockFileEx`, `CancelIo`, event close, and `GetOverlappedResult`.

[A] It passes when every pending request reaches one collected terminal result before `ovlp`/event teardown; aborted requests return the existing busy class, granted races return `SQLITE_OK` with matching masks, and a non-feature build follows its unchanged path.

## Missing

[O] No reproduction was executed, so terminal behavior of the timeout/cancellation race, its reach across Windows filesystems, and user-visible impact remain unobserved.

[A] A publication decision also needs the focused trace above, a re-check of current Fossil/Forum/GitHub activity, and the user's Mode and Target selection.

## Resume

Index: Reproduce timed SHM race
Next: On Windows, trace a two-process WAL SHM timeout race through `LockFileEx`, `CancelIo`, and final completion.
Done when: A recorded trace establishes the terminal result and proves whether `ovlp` and its event outlive it.
