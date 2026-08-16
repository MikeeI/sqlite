# ISSUE-003 — FTS5: highlighting rescans output prefixes

State: Hold
Mode: Undecided
Target: Undecided
Location: Not published.
Priority: High
Confidence: Medium
Type: performance
Created: 2026-08-15
Updated: 2026-08-16
Source: `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`

## Root

Root [S]: `fts5HighlightAppend()` appends with `sqlite3_mprintf("%z%.*s", ...)` for every output fragment.
The `%z` optimization adopts the old allocation but calls `strlen()` on the complete accumulated prefix each time.

## Reach and impact

Reach [S]: FTS5 `highlight()` and final `snippet()` rendering share this append path on matched rows.
Impact [S]: `A` appends over `L` output bytes repeatedly rescan prefixes; small uniform fragments approach quadratic work.
Impact [N]: Typical append counts, output sizes, latency, allocation cost, and end-to-end impact are not measured.

## Evidence

- [S] `ext/fts5/fts5_aux.c:138-147` — every fragment routes through a new `%z` formatting call.
- [S] `ext/fts5/fts5_aux.c:175-224` — match boundaries can emit multiple fragments per highlighted range.
- [S] `ext/fts5/fts5_aux.c:233-287` — `highlight()` uses the shared builder and frees the final allocation.
- [S] `ext/fts5/fts5_aux.c:532-583` — `snippet()` uses the same builder for final rendering.
- [S] `src/printf.c:834-860` — `%z` adopts the allocation after computing the accumulated length with `strlen()`.
- [S] https://sqlite.org/src/raw/src/printf.c?ci=trunk — canonical trunk retains the `%z` scan; checked 2026-08-15.

## Prior art

Bounded searches, checked 2026-08-16: SQLite Bug Forum, User Forum, canonical Fossil history, and `sqlite/sqlite` GitHub activity.

- Distinct: https://sqlite.org/bugs/forumpost/9f9db89c9fbeca27120d275bf43dceab36f17349634627a0f9b396e8c0391cd9 — FTS3/FTS4 integer overflow, not FTS5 prefix scans.
- Related builder/correctness history: https://sqlite.org/src/info/ca5d44042aa7461dcc8b700b0763df4df9d4a891
- Related builder/correctness history: https://sqlite.org/src/info/d570aa02f79b1d7d3889e33f9eebab1b7edcf5231b1357451eed9a538607de54
- Related builder/correctness history: https://sqlite.org/src/info/8f5e9c192ff2820d8cfb076ab28f30697d10c22710583d6c7fd7019c4a0ea795
- No direct FTS5 highlight prefix-scan record was identified in bounded User Forum (https://sqlite.org/forum/) and `sqlite/sqlite` activity (https://github.com/sqlite/sqlite/commits/master) searches; this is not exhaustive.

Target fit: consider a new SQLite Bug Forum thread only after reproducible measurements; Target remains Undecided.

## Direction

Let `HighlightContext` own one `sqlite3_str` builder and append fragments with their known byte lengths.
Finish once and transfer or free the result through one explicit lifecycle while preserving the first SQLite error.

## Bounds

- Preserve: exact bytes, markers, `NULL` handling, UTF-8 offsets, column-range behavior, tokenizer errors, and OOM.
- Exclude: snippet scoring, tokenization, match coalescing, generic printf changes, and unrelated string construction.
- Cost: every return path must finalize or free the builder exactly once without changing result ownership.

## Verification

- `./testfixture ../ext/fts5/test/fts5aux.test` → all existing highlight outputs and errors remain unchanged.
- `./testfixture ../ext/fts5/test/fts5af.test` → all existing snippet outputs remain unchanged.
- Increasing-match benchmark → render time scales near-linearly and output matches the baseline byte-for-byte.

## Missing

- Representative baseline and candidate measurements with variance and a correctness guard.
- A later currentness and target-fit recheck before choosing a target.
- User-selected Mode and Target.

## Resume

Index: Benchmark highlight appends
Next: Define and run a high-match `highlight()` baseline on current `upstream/master`.
Done when: Repeated runs record exact schema, data, query, append counts, output bytes, latency, and variance.

## Performance evidence

Workload: Synthetic evaluator defined below; real-world representativeness is not established.
Baseline [N]: Not measured.
Candidate [N]: No correction is implemented or measured.
Guard [N]: Existing highlight and snippet output tests have not run for a candidate.
Boundary [N]: Prefix-scan cost, allocations, realistic output sizes, and end-to-end impact remain unmeasured.

### Benchmark plan

Baseline: current Source commit; candidate: isolated same-base change only.
Environment: Ubuntu 24.04.4, AMD 5950X, GCC 13.3.0, optimized build, pinned CPU 4.

Workload: `fts5(body,detail=full)` with one deterministic document of `alpha x ` repeated M times.
Execute `MATCH alpha` and `highlight(ft,0,'<b>','</b>')` for M=128, 256, 512, 1024, 2048, 4096.
Setup and statement prepare are untimed.
The SQL measures the complete `highlight()` auxiliary call, including tokenization and match iteration, while excluding `snippet()` scoring.

Primary metric: cycles per complete call at M=4096.
Run 3 warmups, then fixed baseline-calibrated repetitions lasting at least 250ms, across 15 AB/BA pairs.
Report median ratio and MAD.
Secondary attribution: collect a `perf record` symbol profile to quantify CPU share in `fts5HighlightAppend()`/`strlen()`.

Acceptance: at least 25% cycle reduction, scaling exponent at most 1.25, and peak heap at most 10% above baseline.
Require byte-identical `highlight()` and `snippet()` rendering.
Guard empty, `NULL`, and long markers; adjacent, overlapping, and trigram matches; columns and locales; tokenizer errors; and OOM at builder create, grow, and finish.

Candidate ownership: the builder owns its allocation until `finish` transfers it; every error path performs exactly-once cleanup.
Exercise `fts5aux`, `fts5af`, `fts5fault4`, and `fts5fault9` for output, OOM, and lifecycle guardrails.

Stop: do not pursue if `perf record` attributes under 10% of realistic CPU cost to `fts5HighlightAppend()`/`strlen()`, or lifecycle complexity outweighs the gain.
