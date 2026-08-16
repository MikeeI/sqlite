# ISSUE-001 — window: first_value retains partition rows

State: Ready
Mode: Pull request
Target: New pull request
Location: Not published.
Priority: High
Confidence: High
Type: performance
Created: 2026-08-15
Updated: 2026-08-16
Source: `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`

## Root

Root [S]: `windowCacheFrame()` classifies every fast-path `first_value()` frame as requiring cached rows.
For an unbounded frame start, this prevents `WINDOW_RETURN_ROW` cleanup.

## Reach and impact

Reach [S]: Built-in `first_value()` queries with no `EXCLUDE` clause and an unbounded frame start are affected.
Impact [S]: The ephemeral table retains all rows read for the current partition instead of deleting returned rows.
Impact [O]: A synthetic 1m-row case retained 295,223,440 bytes; realistic frequency and temp I/O remain unmeasured.

## Evidence

- [S] `src/window.c:1416-1424` — `EXCLUDE` selects a distinct full-scan path; the finding concerns the fast path.
- [S] `src/window.c:2029-2042` — `windowCacheFrame()` returns true for every `first_value()` window.
- [S] `src/window.c:2851-2862` — unbounded starts select `WINDOW_RETURN_ROW` only when caching is not required.
- [S] `src/window.c:2930-2933` — every input row is inserted into the shared ephemeral table.
- [S] https://sqlite.org/src/raw/src/window.c?ci=trunk — canonical trunk retains this control flow; checked 2026-08-15.

## Prior art

Coverage [S]: SQLite forums, canonical Fossil history, and `sqlite/sqlite` GitHub activity were searched.
The search was checked on 2026-08-16.
Forum [S]: https://sqlite.org/forum/forumpost/c65d4d2431d285585968cf7210fc7acb76f38ad558db6d681e5c5698ae23acf9 — Distinct: huge `FOLLOWING`-bound runtime, not unbounded-start row retention.
Fossil [S]: https://sqlite.org/src/info/e7a91f12282afb5d5d7d78397a11d18e0268ee0c931d85e21fce00d13929494e — Related cache-reduction history.
Fossil [S]: https://sqlite.org/src/info/6ad553192051eaa0c6d929baacde2de07b93c6d09de861028bbce55a2c9bfdd3 — Related cache-reduction history.
Gaps: Coverage does not claim exhaustive absence; representative workload frequency remains unresolved.
Target fit: The user selected a proof-of-concept New pull request.
Publication requires approval of the exact target and draft plus SQLite's accepted submission path.

## Direction

Keep the first frame value and frame-emptiness state outside the retained row set for the eligible fast path.
Allow returned rows to be deleted when no coalesced window function or frame rule still requires random row access.

## Bounds

- Preserve: results for `ROWS`, `RANGE`, `GROUPS`, peers, empty frames, `NULL`, `EXCLUDE`, OOM, and temp-store modes.
- Exclude: `nth_value()`, `lead()`, `lag()`, the `EXCLUDE` full-scan path, public APIs, and file formats.
- Cost: specialized VDBE state and regression risk across mixed window functions and frame boundaries.

## Verification

- `./testfixture ../test/window3.test` → all existing `first_value()` frame results remain unchanged.
- `./testfixture ../test/windowfault.test` → allocation-failure behavior remains correct.
- Increasing single-partition benchmark → retention becomes bounded while every result matches the baseline.

## Missing

- External publication awaits exact user approval and SQLite's accepted submission path.

## Resume

Index: Approve exact pull request
Next: Obtain approval for the exact target and draft below.
Done when: The approved pull request is published and its URL is recorded.

## Implementation

Branch: `perf/window-first-value-retention`
Base: `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`
Scope: Bound retained rows for eligible unbounded-start `first_value()` frames without changing SQL results.
Commit: `58af32584f`
Push: `origin/perf/window-first-value-retention`
Checks:
- `./testfixture ../test/window3.test` → 0 errors.
- `./testfixture ../test/windowfault.test` → 0 errors across 5,601 tests.
- `sudo -u nobody env CCACHE_DISABLE=1 ./testfixture ../test/testrunner.tcl mdevtest` → 0 errors across 977,532 tests.
- Fixed 15-pair benchmark → exact payload guards passed at all five sizes.
- Contribution diff against the base contains only `src/window.c`.

## Performance evidence

Workload: Synthetic single-partition evaluator; real-world representativeness is not established.
Baseline [O]: Peak memory rose from 5,165,632 bytes at 10k rows to 295,223,440 bytes at 1m rows.
Candidate [O]: Peak memory was 2,072,480 bytes at every size; the 1m median ratio was 0.007020.
Candidate [O]: The memory-scaling exponent changed from 0.885844 to 0.000000.
Candidate [O]: At 1m rows, elapsed-time and cycle ratios were 0.447511 and 0.451618 respectively.
Guard [O]: All expected-payload guards and existing window, fault-injection, and development tests passed.
Boundary [N]: Temp I/O, representative workloads, and real-world frequency remain unmeasured.

### Benchmark plan

Baseline: current Source commit `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`.
Candidate: commit `58af32584f` in an isolated worktree at the same base.
Environment: Ubuntu 24.04.4, AMD Ryzen 9 5950X, GCC 13.3.0.
Compilation used `-O2 -DNDEBUG -g -fno-omit-frame-pointer`.
Isolation: each sample used a fresh process and connection pinned with `taskset -c 4`.
Workload: an on-disk `t(id INTEGER PRIMARY KEY,payload BLOB)` held deterministic unique 256-byte payloads.
The five row counts were 10k, 30k, 100k, 300k, and 1m.
Settings: `PRAGMA temp_store=MEMORY; PRAGMA cache_size=-2048; PRAGMA mmap_size=0;`.
SQL:
`SELECT first_value(payload) OVER (ORDER BY id ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) FROM t WHERE id<=?1 ORDER BY id;`.
Plan [O]: setup and prepare were untimed, and EQP showed no independent sorter.
Primary [O]: the evaluator reset `SQLITE_STATUS_MEMORY_USED` high-water immediately before stepping.
It recorded the high-water value minus current memory at reset.
Protocol [O]: each build and size used three warmups and 15 paired AB/BA samples pinned to CPU 4.
Analysis [O]: results use the median candidate/baseline ratio and MAD.
`perf stat` counters and Max RSS were collected as secondary measures.
Acceptance [O]: the 1m peak delta fell by at least 80%, and the scaling exponent was at most 0.25.
Runtime did not regress, and every row returned the exact expected payload.
Exponent: ordinary-least-squares slope of log2 median peak delta against log2 row count.
Guards [O]: `window3`, `windowfault`, and `mdevtest` passed.
Stop [O]: baseline memory scaling was within 0.75–1.25, and EQP showed no independent sorter.

## Draft

Target: `sqlite/sqlite` — New pull request from `MikeeI:perf/window-first-value-retention`

Title: Stream eligible first_value() window frames

Body:

### Summary

`windowCacheFrame()` retains partition rows whenever the fast path includes `first_value()`.
Eligibility is restricted to the exact `ROWS UNBOUNDED PRECEDING ... CURRENT ROW` case.
The new path uses register-backed state and does not retain prior partition rows.

### Evidence

- `src/window.c:windowCacheFrame()` classified every `first_value()` window as requiring cached rows.
- A fixed synthetic evaluator grew from 5,165,632 bytes at 10k rows to 295,223,440 bytes at 1m rows.
- The candidate used 2,072,480 bytes at every size; its 1m candidate/baseline median ratio was 0.007020.
- Reproducible evaluator: https://github.com/MikeeI/sqlite/tree/personal/issues/evidence/ISSUE-001

### Changes

- Stream eligible groups through one register-backed input record.
- Retain the first value in VDBE registers.
- Keep the generic buffered path for `EXCLUDE`, other frames, and any coalesced function requiring random row access.

### Risks and boundaries

- The specialized path preserves the existing one-row delay, partition reset, `NULL`, OOM, and result semantics.
- The change is internal to window execution and does not alter public APIs, SQL syntax, or file formats.

### Verification

- `./testfixture ../test/window3.test` — 0 errors.
- `./testfixture ../test/windowfault.test` — 0 errors across 5,601 tests.
- `sudo -u nobody env CCACHE_DISABLE=1 ./testfixture ../test/testrunner.tcl mdevtest` — 0 errors across 977,532 tests.
- Fixed 15-pair benchmark — exact payload guards passed at all five sizes; the 1m elapsed-time ratio was 0.447511.

I checked the relevant issues, comments, pull requests, and discussions; this pull request is not a duplicate.
