# ISSUE-006 — CLI .restore: missing source can clear target

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

Root [S]: `.restore` opens its `FILE` source with `sqlite3_open()` before `sqlite3_backup_init()`.
Behavior [S]: `sqlite3_open()` always uses `SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE`; a missing creatable source becomes an empty database, then the backup API copies that source into `zDb`.
Failure [S]: the source-open error branch is bypassed and a successful copy maps `SQLITE_DONE` to shell success, so a missing source can silently replace target contents.

## Reach and impact

Reach [S]: CLI callers of `.restore ?DB? FILE` can select `main` or a named destination database through `zDb`.
Reach [S]: the backup API accepts distinct source and destination handles and supports an in-memory destination, so the fault is not limited to file-backed `main`.
Impact [O]: Linux x86_64 debug-shell reproduction cleared a populated file-backed target after restoring a missing source.
Impact [N]: affected-user frequency, production data loss, and platform/VFS coverage are not measured.

## Evidence

- [S] `src/shell.c.in:11440-11466` dispatches `.restore`, defaults `zDb` to `main`, calls `sqlite3_open(zSrcFile, &pSrc)`, then initializes a backup from `pSrc` into `p->db/zDb`; canonical anchor: https://sqlite.org/src/file?name=src/shell.c.in&ci=db05b203f47e6ed0792ac011b18e0da98275e900407436798ca28d5f7dd2e094#ln11440
- [S] `src/shell.c.in:11472-11489` retries source `SQLITE_BUSY`, finishes the backup, maps `SQLITE_DONE` to `0`, reports only later errors, and closes `pSrc`; mirror anchor: https://github.com/sqlite/sqlite/blob/f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb/src/shell.c.in#L11472-L11489
- [S] `src/sqlite.h.in:3766-3790` specifies that `sqlite3_open()` uses read-write/create, while `sqlite3_open_v2(..., SQLITE_OPEN_READWRITE, ...)` requires an existing file and may fall back to read-only; authoritative contract: https://sqlite.org/c3ref/open.html
- [S] `src/sqlite.h.in:9643-9752` specifies source-to-destination copying, destination transaction rollback on unfinished backup, and required `sqlite3_backup_finish()` cleanup; authoritative contract: https://sqlite.org/c3ref/backup_finish.html
- [S] `src/sqlite.h.in:3874-3929` defines global URI enablement, URI `mode`, and URI/flag restriction checks; `sqlite3_open()` currently supplies no explicit URI flag.
- [S] The assigned official-mirror revision exists as https://github.com/sqlite/sqlite/commit/f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb with canonical `FossilOrigin-Name` `db05b203f47e6ed0792ac011b18e0da98275e900407436798ca28d5f7dd2e094`.
- [S] Current CLI documentation exposes `.restore ?DB? FILE` as restoring `FILE` into the selected database: https://sqlite.org/cli.html

## Bug reproduction

Environment: Linux x86_64 debug `bld/sqlite3` built from `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`.
Reproduction: `d=$(mktemp -d); ./bld/sqlite3 "$d/dest.db" 'CREATE TABLE keep(x); INSERT INTO keep VALUES(1);' && printf 'before|%s\n' "$(./bld/sqlite3 "$d/dest.db" 'SELECT count(*) FROM keep;')" && ./bld/sqlite3 "$d/dest.db" ".restore $d/missing.db" && printf 'missing-size=%s\n' "$(wc -c < "$d/missing.db")" && printf 'after|%s\n' "$(./bld/sqlite3 "$d/dest.db" "SELECT count(*) FROM sqlite_schema WHERE name='keep';")"`.
Actual [O]: `before|1`, `missing-size=0`, and `after|0`; the missing source was created empty and `keep` was absent after `.restore`.
Expected [A]: a missing source should make a non-creating open fail before `sqlite3_backup_init()`; `keep` should remain present.

## Prior art

- [S] SQLite 3.6.11 introduced both the hot-backup interface and CLI `.backup`/`.restore`: https://sqlite.org/releaselog/3_6_11.html
- [S] Forum thread https://sqlite.org/forum/info/30464637e0acae43dedd0c5e174bb84f3928b86b933c21623a16bfa4fab66056 classifies `.restore` as the Backup API read-in portion; it does not discuss missing-source creation.
- [S] Forum thread https://sqlite.org/forum/info/f3860150330726415e42590f5783e7a8526dd33df6accaf2b14fc1eb01e4dbba distinguishes CLI `.restore` from SQL and names the Backup API alternative; it does not discuss this root cause.
- [S] Local mirror history traces this source-open form to Fossil check-in `003e1d62189e9e37f901d86a696cfccd22bd3b38` (“Add `.backup` and `.restore` commands”); canonical history target: https://sqlite.org/src/info/003e1d62189e9e37f901d86a696cfccd22bd3b38
- [N] GitHub searches of official `sqlite/sqlite` issues and pull requests for `.restore` and `restore data loss` returned zero results on 2026-08-16; zero results do not prove no duplicate: https://github.com/sqlite/sqlite
- [N] Direct canonical Fossil `finfo` and `vdiff` reads returned Browser Verification, so file-history and ticket prior art remain incomplete rather than negative evidence.

Target fit [N]: Undecided; the related Forum threads do not own the missing-source root cause, and canonical Fossil prior-art review is incomplete.

## Direction

Direction [A]: replace only the `.restore` source open with `sqlite3_open_v2(zSrcFile, &pSrc, SQLITE_OPEN_READWRITE, 0)`.
Effect [A]: omitting `SQLITE_OPEN_CREATE` makes an absent normal filesystem source fail at the existing error/`close_db(pSrc)` return before `open_db()` or `sqlite3_backup_init()` can touch the target.
Preserve [A]: retain the existing `zDb` destination selection, target connection and in-memory behavior, source busy retry, `sqlite3_backup_finish()`, and every `pSrc` close path.

## Bounds

- [S] Preserve existing filesystem sources, including sources opened read-only after a failed read-write attempt, as documented for `SQLITE_OPEN_READWRITE`.
- [S] Preserve current global URI enablement by passing no `SQLITE_OPEN_URI` flag; verify existing `file:` sources with `mode=ro` and `mode=rw`.
- [S] A source URI with `mode=rwc` requests creation and must not retain creation authority under this direction; confirm the intended error before claiming URI compatibility.
- [S] `:memory:`, empty filenames, and `file:` URIs with `mode=memory` create temporary or memory databases under documented special rules; resolve their source-policy compatibility explicitly.
- [S] Preserve named destinations, attached-database names, in-memory target page-size failure, busy/locked reporting, rollback on unfinished backup, and source-handle cleanup.
- Exclude: `.backup`/`.save` semantics, backup retry redesign, output redesign, generic URI policy, new tests, and unrelated source changes.

## Verification

Test decision: none; this record authorizes no source or test change.
Verification [O]: the `Bug reproduction` establishes the present missing-source, empty-source, and cleared-target behavior on Linux x86_64 debug shell.
Verification [N]: no formatter, linter, or repository test suite was run for this record-only change.
Candidate check [A]: rerun the portable reproduction and verify an open error, no created missing source, and `keep` retained; cover existing read-only source, `file:` `ro`/`rw`/`rwc` URIs, named and in-memory destinations, source busy/locked behavior, and every source-handle cleanup path.

## Missing

- [N] Direct canonical Fossil history and ticket review for the same missing-source behavior remains blocked by Browser Verification.
- [N] Compatibility disposition for `mode=rwc`, `:memory:`, empty, and `mode=memory` source names is not yet verified.
- [N] User-selected Mode and Target.

## Resume

Index: Review restore prior art
Next: Complete canonical Fossil history and ticket review using the `Bug reproduction` missing-source, `missing-size=0`, `after|0` discriminator.
Done when: Every plausible Fossil candidate is classified as duplicate or nonduplicate with an accessible URL, and no canonical-history blocker remains.
