# ISSUE-001 — window: first_value retains partition rows

State: Hold
Mode: Undecided
Target: Undecided
Location: Not published.
Priority: High
Confidence: Medium
Type: performance
Created: 2026-08-15
Updated: 2026-08-15
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

Coverage: local ledger IDs, titles, symptoms, root causes, and symbols; checked=2026-08-15.
Gaps: Fossil history and tickets, SQLite Forum, GitHub activity, and release notes were not searched.

Target fit: Undecided — representative measurements and external prior-art research are incomplete.

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

- Representative baseline and candidate measurements with variance and a correctness guard.
- External prior-art coverage and current target fit.
- User-selected Mode and Target.

## Resume

Index: Benchmark partition retention
Next: Define and run a large single-partition `first_value()` baseline on current `upstream/master`.
Done when: Repeated runs record exact SQL, rows, latency, peak RSS, temp I/O, and variance.

## Performance evidence

Workload: Not established.
Baseline [N]: Not measured.
Candidate [N]: No correction is implemented or measured.
Guard [N]: Existing result-equivalence and fault tests have not run for a candidate.
Boundary [N]: Peak ephemeral storage, temp I/O, latency, and end-to-end impact remain unmeasured.
