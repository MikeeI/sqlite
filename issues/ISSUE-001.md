# ISSUE-001 — window: first_value retains partition rows

State: Hold
Mode: Pull request
Target: New pull request
Location: Not published.
Priority: High
Confidence: Medium
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
Impact [N]: Peak memory, temp-file I/O, latency, and real-world frequency are not measured.

## Evidence

- [S] `src/window.c:1416-1424` — `EXCLUDE` selects a distinct full-scan path; the finding concerns the fast path.
- [S] `src/window.c:2029-2042` — `windowCacheFrame()` returns true for every `first_value()` window.
- [S] `src/window.c:2851-2862` — unbounded starts select `WINDOW_RETURN_ROW` only when caching is not required.
- [S] `src/window.c:2930-2933` — every input row is inserted into the shared ephemeral table.
- [S] https://sqlite.org/src/raw/src/window.c?ci=trunk — canonical trunk retains this control flow; checked 2026-08-15.

## Prior art

Coverage [S]: SQLite Bug Forum and User Forum, canonical Fossil history, and `sqlite/sqlite` GitHub activity searched; checked 2026-08-16.
Forum [S]: https://sqlite.org/forum/forumpost/c65d4d2431d285585968cf7210fc7acb76f38ad558db6d681e5c5698ae23acf9 — Distinct: huge `FOLLOWING`-bound runtime, not unbounded-start row retention.
Fossil [S]: https://sqlite.org/src/info/e7a91f12282afb5d5d7d78397a11d18e0268ee0c931d85e21fce00d13929494e — Related cache-reduction history.
Fossil [S]: https://sqlite.org/src/info/6ad553192051eaa0c6d929baacde2de07b93c6d09de861028bbce55a2c9bfdd3 — Related cache-reduction history.
Gaps: Coverage does not claim exhaustive absence; representative measurements and a candidate remain required.
Target fit: User selected a proof-of-concept New pull request; external publication remains contingent on evidence, approval of the exact target and draft, and SQLite's submission path.

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

- Pending baseline benchmark, candidate correction, focused checks, commit, push, and exact pull-request draft.
- SQLite submission agreement remains required before submission.

## Resume

Index: Build retention harness
Next: Build and freeze the documented evaluator on the contribution base.
Done when: The baseline build emits deterministic guard output and the fixed measurement schema.

## Performance evidence

Workload: Synthetic evaluator defined below; real-world representativeness is not established.
Baseline [N]: Not measured.
Candidate [N]: No correction is implemented or measured.
Guard [N]: Existing result-equivalence and fault tests have not run for a candidate.
Boundary [N]: Peak ephemeral storage, temp I/O, latency, and end-to-end impact remain unmeasured.

### Benchmark plan

Baseline: current Source commit `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`.
Candidate: isolated worktree at that base with only the proposed unbounded-start `first_value()` retention correction.
Environment: Ubuntu 24.04.4, AMD Ryzen 9 5950X, GCC 13.3.0; compile with `-O2 -DNDEBUG -g -fno-omit-frame-pointer`.
Isolation: one fresh dedicated process and connection per sample, pinned with `taskset -c 4`; no concurrent SQLite work.
Workload: temporary on-disk `t(id INTEGER PRIMARY KEY,payload BLOB)` with deterministic unique 256-byte payloads; N=10k,30k,100k,300k,1m.
Settings: `PRAGMA temp_store=MEMORY; PRAGMA cache_size=-2048; PRAGMA mmap_size=0;`.
SQL: `SELECT first_value(payload) OVER (ORDER BY id ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) FROM t WHERE id <= ?1;`.
Plan [N]: setup and prepare are untimed; `?1` equals N; verify EQP has no independent sorter before every measured series.
Primary [N]: reset `SQLITE_STATUS_MEMORY_USED` high-water immediately before stepping; record high-water minus current memory at reset.
Protocol [N]: three warmups per build and N, then 15 paired samples alternating AB/BA (eight AB, seven BA), pinned to CPU 4.
Analysis [N]: report median candidate/baseline ratio and MAD; collect `perf stat` counters and Max RSS as secondary measures.
Acceptance [N]: report only if the candidate reduces the 1m peak delta by >=80%, has a <=0.25 memory-scaling exponent, regresses runtime by <=3%, and returns the exact expected payload for every row.
Exponent [N]: ordinary-least-squares slope of `log2(max(1, median peak delta))` against `log2(N)` across the five N values.
Guards [N]: compare `ROWS`, `RANGE`, and `GROUPS`; empty and `NULL` frames; `EXCLUDE`; mixed `nth_value()`, `lead()`, and `lag()`; then `window3`, `windowfault`, and `devtest`.
Stop [N]: stop rather than report if baseline memory scaling is outside 0.75–1.25, EQP shows an independent sorter, or another SQLite connection confounds the status counter.
