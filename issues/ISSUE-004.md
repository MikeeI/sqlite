# ISSUE-004 — vtab: busy close rolls back state

State: Hold
Mode: Undecided
Target: Undecided
Location: Not published.
Priority: High
Confidence: High
Type: correctness
Created: 2026-08-16
Updated: 2026-08-16
Source: `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`

## Root

Root [S]: Legacy `sqlite3_close()` calls `sqlite3VtabRollback()` before `connectionIsBusy()` can return `SQLITE_BUSY`.
Mechanism [S]: `callFinaliser()` calls each enrolled module's `xRollback`, clears `aVTrans`, resets savepoint state, and unlocks it.
Consequence [S]: The busy return defers ordinary B-tree rollback to zombie destruction, so one failed close can split vtab and B-tree state.

## Reach and impact

Reach [S]: A legacy `sqlite3_close()` call reaches this path when a module completed `xBegin()` and its `VTable` remains in `db->aVTrans`, while an unfinalized VDBE or unfinished backup makes `connectionIsBusy()` true.
Impact [O]: A counting module observed `xRollback` during `sqlite3_close()==SQLITE_BUSY`; after finalizing the blocker, `COMMIT` committed the ordinary row without `xSync` or `xCommit`.
Impact [S]: The observed split follows the source path: vtab finalization precedes the busy return, while ordinary B-tree rollback only occurs during zombie destruction.
Impact [N]: No stock virtual table, application, data-loss case, frequency, or user-visible reach has been reproduced or measured.
Concurrency [S]: `sqlite3Close()` holds `db->mutex`; disconnect takes all B-tree mutexes, and the finalizer's unlock may invoke `xDisconnect`, so any correction changes callback and shared-cache lifetime ordering.

## Evidence

- [O] Canonical `trunk` check-in `db05b203f47e6ed0792ac011b18e0da98275e900407436798ca28d5f7dd2e094` maps to the source Git commit; checked 2026-08-16: https://sqlite.org/src/info/db05b203f47e6ed0792ac011b18e0da98275e900407436798ca28d5f7dd2e094
- [S] `src/main.c:1270-1278` — `connectionIsBusy()` tests `db->pVdbe` and each B-tree backup, not `aVTrans`: https://github.com/sqlite/sqlite/blob/f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb/src/main.c#L1270-L1278
- [S] `src/main.c:1298-1318` — `disconnectAllVtab()` then `sqlite3VtabRollback()` precede the legacy busy return; the in-source rationale is vtab-owned prepared statements: https://github.com/sqlite/sqlite/blob/f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb/src/main.c#L1298-L1318
- [S] `src/vtab.c:974-992,1024-1026` — `callFinaliser()` detaches `aVTrans`, calls `xRollback`, clears `iSavepoint`, unlocks every `VTable`, frees the array, and zeros `nVTrans`: https://github.com/sqlite/sqlite/blob/f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb/src/vtab.c#L974-L1026
- [S] `src/vtab.c:760-764,1046-1088` — successful `xBegin` enrolls and locks the `VTable`; `src/vtab.c:205-221` calls `xDisconnect` when finalizer unlock reaches zero: https://github.com/sqlite/sqlite/blob/f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb/src/vtab.c#L760-L764
- [S] `src/main.c:1393-1419,1513-1545` — only the zombie close path calls `sqlite3RollbackAll()`, which rolls B-trees then vtabs; legacy busy returns before this path: https://github.com/sqlite/sqlite/blob/f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb/src/main.c#L1393-L1419
- [S] `src/sqlite.h.in:322-346` and https://sqlite.org/c3ref/close.html — `sqlite3_close()` leaves a busy connection open; destruction with an open transaction rolls it back.
- [S] https://sqlite.org/vtab.html#xrollback — `xRollback` rolls back a virtual-table transaction; `xBegin` must be followed by exactly one `xCommit` or `xRollback`.
- [S] https://sqlite.org/vtab.html#the_xsavepoint_xrelease_and_xrollbackto_methods — virtual-table savepoint callbacks are valid only between `xBegin` and `xCommit` or `xRollback`.
- [S] Canonical sources at this revision: https://sqlite.org/src/raw/src/main.c?ci=db05b203f47e6ed0792ac011b18e0da98275e900407436798ca28d5f7dd2e094 and https://sqlite.org/src/raw/src/vtab.c?ci=db05b203f47e6ed0792ac011b18e0da98275e900407436798ca28d5f7dd2e094

## Bug reproduction

Environment: `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`; Ubuntu 24.04 Linux x86_64; debug `bld/libsqlite3.a`.
Reproduction: Register an in-memory `txprobe` vtab that counts `xBegin/xSync/xCommit/xRollback`; `BEGIN; INSERT` into `ordinary` and `vt`; retain prepared `SELECT 1`; call `sqlite3_close()`, finalize it, then `COMMIT` and count `ordinary`.
Actual [O]: `close=5 autocommit=0 begin=1 sync=0 commit=0 rollback=1 update=1`; then `commit=0 rows=1 begin=1 sync=0 commit_cb=0 rollback=1`.
Expected [A]: A busy legacy close should leave the vtab transaction open; later `COMMIT` must preserve ordinary/vtab atomicity and invoke the vtab commit path.

## Prior art

Coverage [O]: Read current local ledger records; canonical Fossil check-ins `db05b203f4`, `6071b7cce0`, `c4b8621125`, and `f88e1d0357`; Forum queries for close/vtab and exact internals; GitHub mirror activity and issue, pull-request, commit, and code searches; checked 2026-08-16.
Strongest candidate [O]: Canonical check-in `6071b7cce067c807e040283fc4b7449dc6eca498` says a busy `sqlite3_close()` must “not roll back any active transaction”: https://sqlite.org/src/info/6071b7cce067c807e040283fc4b7449dc6eca498
Conflict [S]: Current code still finalizes vtab state before its busy check.
Classification [A]: This is antecedent semantic conflict, not a proven duplicate; its linked ticket `e636a050b709` is unreadable without Fossil's visual robot challenge, so its intended vtab scope is unknown.
Related [O]: `c4b8621125ce77308b06692d92f70586b10055a9` restored legacy busy-close behavior and introduced deferred `sqlite3_close_v2()` semantics: https://sqlite.org/src/info/c4b8621125ce77308b06692d92f70586b10055a9
Related [O]: Forum thread `47bf2923` reported an FTS4/shared-cache busy close, but its example first raised `unsafe use of virtual table` and did not establish this root cause: https://sqlite.org/forum/info/47bf292320fd3b9797a76e3f2a01e2f9abb493409afff09d68ba9f35ab7addb7
Related [O]: Forum thread `137c7662` and check-in `f88e1d0357` fixed FTS5 retaining an internal blob handle that interfered with interruption; they confirm hidden extension resources matter, but not this close rollback ordering: https://sqlite.org/forum/forumpost/137c7662b389fa18 and https://sqlite.org/src/info/f88e1d0357
GitHub [O]: The official mirror was active on `master` (updated 2026-08-16) and contains the pinned commit: https://github.com/sqlite/sqlite and https://github.com/sqlite/sqlite/commit/f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb
GitHub [O]: Native searches `sqlite3_close virtual table OR xRollback` for issues and pull requests, `sqlite3_close vtab rollback` for commits, and `sqlite3VtabRollback path:src/main.c` for code returned zero results; search-index scope means this is not absence proof.
Target fit [A]: Hold; the observed external-statement split, the untested private-vtab-statement case, and the inaccessible ticket leave duplicate status, intent, and correction value unresolved.

## Direction

Direction [A]: Confirm the private-vtab-statement case before prescribing code. If it agrees, make the legacy busy path failure-atomic: a call that returns `SQLITE_BUSY` must not finalize enrolled vtab transactions or detach them from still-live B-tree state.
Direction [A]: Do not merely move `connectionIsBusy()` upward; the source rationale says rollback runs first because a vtab may retain private prepared statements. A viable design must still release those resources when close can succeed.

## Bounds

- Preserve [S]: legacy `sqlite3_close()` return and open-handle behavior, plus `sqlite3_close_v2()` zombie deferral and final destruction after dependent resources close.
- Preserve [S]: private vtab statements, `xBegin` to exactly-one `xCommit`/`xRollback`, deferred `xDisconnect`, callback reentrancy, error ownership, and ref-count lifetime.
- Preserve [S]: savepoint, release, and rollback-to ordering; `callFinaliser()` currently clears each `iSavepoint` with the final transaction callback.
- Preserve [S]: ordinary/vtab transaction atomicity, B-tree mutex acquisition and schema-reset ordering, shared-cache safety, automatic destruction rollback, and OOM/error cleanup.
- Exclude [S]: public API redesign, auto-finalizing application statements, changing vtab module contracts, or a source/test change before reproduction and prior-art scope are settled.

## Verification

Test decision: none. No repository test suite, formatter, or linter ran; the observed standalone C reproduction below is not a repository test.
- [O] The standalone `txprobe` scenario under `Bug reproduction` produced `SQLITE_BUSY`, one `xRollback`, then a successful ordinary-table commit without `xCommit`.
- [N] Repeat with a vtab-private prepared statement, because the current ordering names that lifecycle as its reason; prove a candidate neither leaks it nor converts a clean close into `SQLITE_BUSY`.
- [N] Exercise `sqlite3_close_v2()`, nested savepoints, callback reentrancy, shared cache, and an allocation-failure path; verify exactly-once finalization and ordinary/vtab outcome equivalence.

## API and compatibility

Callers [S]: C and host-language bindings using `sqlite3_close()` with an active VDBE or backup and a transactional virtual table; `sqlite3_close_v2()` callers share finalization machinery but not the legacy busy return.
Contract [S]: `sqlite3_close()` returns `SQLITE_BUSY` and leaves the connection open for unfinalized statements, BLOBs, or backups; `sqlite3_close_v2()` instead makes it an unusable zombie.
Compatibility [S]: A failed legacy close must preserve usable connection state without silently rolling back only the vtab portion; successful destruction must retain automatic rollback semantics.
Migration: None.

## Missing

- [N] A deterministic reproduction with a vtab-private prepared statement that records close rc, callback sequence, and post-close commit or rollback outcome.
- [A] Full context for Fossil ticket `e636a050b709`, including whether its “any active transaction” language intentionally excludes vtabs.
- [A] Maintainer-intent evidence for how close must handle private vtab statements without a failed close changing transaction state.
- [N] Stock-module and application reach, data-loss impact, and frequency.
- [A] User-selected Mode and Target; no external draft or publication decision exists.

## Resume

Index: Probe private-vtab busy
Next: Run the `txprobe` scenario with `xBegin` retaining a private prepared statement through the same busy-close and post-finalize commit sequence.
Done when: Exact source/build, close rc, private-statement disposition, xBegin/xSync/xCommit/xRollback counts, and ordinary/vtab outcomes are recorded.
