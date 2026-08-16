# ISSUE-007 — sqldiff: changeset target opens before validation

State: Hold
Mode: Undecided
Target: Undecided
Location: Not published.
Priority: High
Confidence: High
Type: reliability
Created: 2026-08-16
Updated: 2026-08-16
Source: `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`

## Root

Root [S]: `main()` opens `--changeset FILE` with `sqlite3_fopen(..., "wb")` while parsing options, before argument and database validation.
Effect [S]: On non-Windows `sqlite3_fopen` is `fopen`; on Windows it calls `_wfopen`; both documented `w` modes destroy an existing target's contents.
Boundary [S]: The early open is source-proven; the observed truncation below establishes only the missing-argument path, not every later failure path.

## Reach and impact

Reach [S]: Only `sqldiff` calls selecting `--changeset FILE` reach this open; normal SQL output remains `stdout`.
Failure paths [S]: After opening, `cmdlineError()` can reject missing DB arguments, incompatible `--schema --table`, bad options or extra arguments, unreadable or invalid DB1/DB2, failed extension loading, failed `ATTACH`, or invalid auxiliary schema.
Caller [S]: `ext/session/sessiondiff.test:get_changeset` invokes `sqldiff --changeset changeset.bin` only on successful test inputs; no in-tree failure-path caller was found.
Impact [O]: Linux reproduction truncated a pre-existing 4-byte regular target to 0 bytes before reporting a missing database argument.
Impact [N]: Affected scripts, target reuse frequency, filesystem types, and real-world data loss remain unmeasured.

## Evidence

- [S] `tool/sqldiff.c:1929-1939` parses `--changeset`, calls `sqlite3_fopen(argv[++i], "wb")`, then selects `changeset_one_table`.
- [S] `tool/sqldiff.c:1993-2034` validates argument combination, opens and probes DB1/DB2 read-only, loads extensions, attaches DB2, and probes `aux.sqlite_schema` after that open.
- [S] `tool/sqldiff.c:64-73` makes each cited validation failure print an error and `exit(1)`; `tool/sqldiff.c:2036-2059` begins output only afterward.
- [S] `ext/misc/sqlite3_stdio.h:46-55` maps non-Windows `sqlite3_fopen` to `fopen`; `ext/misc/sqlite3_stdio.c:97-117` maps Windows to `_wfopen`.
- [S] POSIX specifies `fopen` mode `w` as truncate-or-create and `b` as non-semantic: https://pubs.opengroup.org/onlinepubs/9799919799/functions/fopen.html
- [S] Microsoft documents `_wfopen` as identical to `fopen` except argument width and documents `w` as destroying an existing file: https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fopen-wfopen?view=msvc-170
- [S] Requested mirror revision source: https://github.com/sqlite/sqlite/blob/f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb/tool/sqldiff.c#L1909-L2059
- [S] Mirror provenance maps that revision to canonical Fossil `db05b203f47e6ed0792ac011b18e0da98275e900407436798ca28d5f7dd2e094`: https://sqlite.org/src/info/db05b203f47e6ed0792ac011b18e0da98275e900407436798ca28d5f7dd2e094
- [S] `ext/session/sessiondiff.test:56-60` is the sole in-tree `--changeset` caller found by exact option search.

## Bug reproduction

Environment: Linux x86_64 debug build from `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`.
Reproduction: In a disposable directory, write `KEEP` to `target.bin`, then run `sqldiff --changeset target.bin only.db` with only one positional database argument.
Actual [O]: Exit 1; stderr begins `./bld/sqldiff: two database arguments required` and includes the help hint; `target.bin` changes from 4 bytes to 0 bytes.
Expected [A]: Argument validation should fail before opening or altering an existing changeset target.

## Prior art

Coverage [S]: Checked local ledger records; current source and history; official `sqldiff` and open APIs; indexed SQLite Forum; and GitHub mirror issues, pull requests, and commits on 2026-08-16.
History [S]: Changeset work-in-progress already opened the target during parsing in Fossil `463e38d765f9d055b63792a8ea15c3782657b07f`; formal addition followed in `f9a3a8391c28cf13d76ec54f471735d35059acea`; the 2024 stdio port retained ordering in `18f784c47d4252bc3696a7e084a1afb9f51f006cf2021292f2103531b8235226`.
Related [S]: Forum https://sqlite.org/forum/info/ec2d429e32 and Fossil `e8b33525fc2b4d609a7be9acc43fbc66638effaa7b092b48568ded6c53c4f4db fixed creation of missing DB inputs by read-only validation, not the `--changeset` output target.
Related [S]: Forum https://sqlite.org/forum/forumpost/c99134e6f0c74deb?hist=&t=h explains empty changesets from session limitations on valid inputs; it does not cover validation-time target truncation.
Related [S]: The official usage says `--changeset FILE` writes a binary changeset into `FILE`, but documents neither failure-time target preservation nor output atomicity: https://sqlite.org/sqldiff.html
GitHub [S]: Repository queries for `sqldiff changeset` and `sqldiff truncate OR truncation OR output` returned no issues or pull requests; relevant commits include https://github.com/sqlite/sqlite/commit/2e32cbc4a589fb80a0423e4d7917eb7e56db3ca7 and https://github.com/sqlite/sqlite/commit/a55901a27cf3de084c906466f0e126e76be0de64.
Gaps [A]: The canonical Fossil file page required browser verification here, and indexed Forum/GitHub search cannot prove no exact-root discussion exists; targeted prior-art review remains incomplete.
Target fit [A]: No existing thread found owns this exact root, but the remaining prior-art review is required before choosing a target.

## Direction

Candidate [A]: Retain the selected changeset path during parsing and open it exactly once immediately before generation, after all argument and database validation/setup succeeds.
Preserve [A]: `--changeset` would continue to select file output and binary changeset generation; no output bytes would be generated before the deferred open.

## Bounds

- Preserve: documented `--changeset FILE` output, option ordering, DB read-only validation, error text, and successful output bytes.
- Exclude: later write, flush, close, rename, and durability failures; temporary-file/rename atomicity; output-target aliases of DB1/DB2; symlinks, special files, and concurrent writers.
- Constraint [S]: Multiple `--changeset` occurrences and combinations with `--rbu` are accepted today; their intended semantics require confirmation before a change.
- Lifecycle: this finding addresses only opening before validation, not later output cleanup or commit semantics.

## Verification

- Candidate [A]: After a correction, prefill a regular target with a sentinel, run `sqldiff --changeset TARGET` without DB arguments, and require exit 1, the argument diagnostic, and byte-identical `TARGET`.
- Candidate [A]: Repeat with a non-SQLite DB1 and DB2 to cover each database-validation boundary without claiming write/close atomicity.
- Checks [O]: Test decision: none. No formatter, linter, or repository test suite ran; the focused CLI reproduction is not a repository test.

## Missing

- Targeted completion of canonical Fossil, SQLite Forum, and GitHub prior-art review for the exact validation-time output root.
- User-selected Mode and Target.

## Resume

Index: Review exact-root prior art
Next: Review current Fossil history plus SQLite Forum and GitHub candidates for `--changeset` with validation-time target truncation.
Done when: Every direct candidate is linked and classified as duplicate, related, fixed, or distinct, and target fit is updated.
