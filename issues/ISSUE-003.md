# ISSUE-003 — FTS5: highlighting rescans output prefixes

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

Root [S]: `fts5HighlightAppend()` appends with `sqlite3_mprintf("%z%.*s", ...)` for every output fragment.
The `%z` optimization adopts the old allocation but calls `strlen()` on the complete accumulated prefix each time.

## Reach and impact

Reach [S]: FTS5 `highlight()` and final `snippet()` rendering share this append path on matched rows.
Impact [S]: Repeated prefix scans approach quadratic work when output uses many small fragments.
Impact [O]: At 4,096 matches, the candidate used 7.70% of baseline cycles in the synthetic evaluator.

## Evidence

- [S] `ext/fts5/fts5_aux.c:138-147` — every fragment routes through a new `%z` formatting call.
- [S] `ext/fts5/fts5_aux.c:175-224` — match boundaries can emit multiple fragments per highlighted range.
- [S] `ext/fts5/fts5_aux.c:233-287` — `highlight()` uses the shared builder and frees the final allocation.
- [S] `ext/fts5/fts5_aux.c:532-583` — `snippet()` uses the same builder for final rendering.
- [S] `src/printf.c:834-860` — `%z` adopts the allocation after computing the accumulated length with `strlen()`.
- [S] https://sqlite.org/src/raw/src/printf.c?ci=trunk — canonical trunk retains the `%z` scan; checked 2026-08-15.

## Prior art

Bounded SQLite forum, Fossil, and `sqlite/sqlite` GitHub searches were checked on 2026-08-16.

- Distinct: https://sqlite.org/bugs/forumpost/9f9db89c9fbeca27120d275bf43dceab36f17349634627a0f9b396e8c0391cd9 — FTS3/FTS4 integer overflow, not FTS5 prefix scans.
- Related builder/correctness history: https://sqlite.org/src/info/ca5d44042aa7461dcc8b700b0763df4df9d4a891
- Related builder/correctness history: https://sqlite.org/src/info/d570aa02f79b1d7d3889e33f9eebab1b7edcf5231b1357451eed9a538607de54
- Related builder/correctness history: https://sqlite.org/src/info/8f5e9c192ff2820d8cfb076ab28f30697d10c22710583d6c7fd7019c4a0ea795
- No direct FTS5 highlight prefix-scan record was identified in bounded User Forum (https://sqlite.org/forum/) and `sqlite/sqlite` activity (https://github.com/sqlite/sqlite/commits/master) searches; this is not exhaustive.

Target fit: The user selected a proof-of-concept New pull request.
Publication requires approval of the exact target and draft plus SQLite's accepted submission path.

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

- External publication awaits exact user approval and SQLite's accepted submission path.

## Resume

Index: Approve exact pull request
Next: Obtain approval for the exact target and draft below.
Done when: The approved pull request is published and its URL is recorded.

## Implementation

Branch: `perf/fts5-highlight-builder`
Base: `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`
Scope: Replace repeated highlight prefix reconstruction with one owned `sqlite3_str` builder.
Commit: `d71abf0230`
Push: `origin/perf/fts5-highlight-builder`
Checks:
- `./testfixture ../ext/fts5/test/fts5aux.test` → 0 errors.
- `./testfixture ../ext/fts5/test/fts5af.test` → 0 errors.
- `./testfixture ../ext/fts5/test/fts5fault4.test` → 0 errors.
- `./testfixture ../ext/fts5/test/fts5fault9.test` → 0 errors.
- Fixed 15-pair benchmark → byte-identical dumps at all six match counts.
- Contribution diff against the base contains only `ext/fts5/fts5_aux.c`.

## Performance evidence

Workload: Synthetic repeated-highlight evaluator; real-world representativeness is not established.
Baseline [O]: Median cycle scaling across 128–4,096 matches had exponent 1.400124.
Candidate [O]: Median cycle scaling exponent was 0.863090.
Candidate [O]: At 4,096 matches, the median cycle ratio was 0.0769576 with MAD 0.00066324.
Candidate [O]: The 4,096-match heap ratio was 0.898095.
Attribution [O]: `perf` attributed 81.17% of baseline samples to `__strlen_avx2`.
Guard [O]: Dumps were byte-identical at every size; highlight, snippet, and fault-injection tests passed.
Boundary [N]: Representative output sizes, allocations, and end-to-end query impact remain unmeasured.

### Benchmark plan

Baseline: current Source commit; candidate: isolated same-base change only.
Environment: Ubuntu 24.04.4, AMD 5950X, GCC 13.3.0, optimized build, pinned CPU 4.

Workload: `fts5(body,detail=full)` with one deterministic document of `alpha x ` repeated M times.
Execute `MATCH alpha` and `highlight(ft,0,'<b>','</b>')` for M=128, 256, 512, 1024, 2048, 4096.
Setup and statement prepare are untimed.
The SQL measures the complete `highlight()` call, including tokenization and match iteration.
It excludes `snippet()` scoring.

Primary metric: cycles per complete call at M=4096.
Run 3 warmups, then fixed baseline-calibrated repetitions lasting at least 250ms, across 15 AB/BA pairs.
Report median ratio and MAD.
Secondary attribution: a `perf record` profile measured `fts5HighlightAppend()` and `strlen()` CPU share.

Acceptance: at least 25% cycle reduction, scaling exponent at most 1.25, and peak heap at most 10% above baseline.
Require byte-identical `highlight()` and `snippet()` rendering.
Guard empty, `NULL`, and long markers plus adjacent, overlapping, and trigram matches.
Also guard columns, locales, tokenizer errors, and OOM during builder create, grow, and finish.

Candidate ownership: the builder owns its allocation until `finish` transfers it.
Every error path performs exactly-once cleanup.
Exercise `fts5aux`, `fts5af`, `fts5fault4`, and `fts5fault9` for output, OOM, and lifecycle guardrails.

Stop if `perf` attributes under 10% of realistic CPU cost to the append path.
Also stop if lifecycle complexity outweighs the gain.

## Draft

Target: `sqlite/sqlite` — New pull request from `MikeeI:perf/fts5-highlight-builder`

Title: Build FTS5 highlight output incrementally

Body:

### Summary

`fts5HighlightAppend()` rebuilt output with `sqlite3_mprintf("%z%.*s", ...)` for every fragment.
This change gives each highlight operation one `sqlite3_str` builder and preserves exact rendered output.

### Evidence

- `ext/fts5/fts5_aux.c:fts5HighlightAppend()` routed every fragment through a new `%z` formatting call.
- `src/printf.c` computes the accumulated `%z` length with `strlen()` before adopting the old allocation.
- At 4,096 matches, `perf` attributed 81.17% of baseline samples to `__strlen_avx2`.
- The candidate's median cycle ratio was 0.0769576 and its cycle-scaling exponent was 0.863090.
- Reproducible evaluator: https://github.com/MikeeI/sqlite/tree/personal/issues/evidence/ISSUE-003

### Changes

- Append fragments with their known byte lengths to one operation-owned builder and finish it once.
- Leave tokenization, match coalescing, snippet scoring, markers, UTF-8 offsets, and output bytes unchanged.

### Risks and boundaries

- Every success and error path finishes, transfers, or frees builder ownership exactly once.
- The change is internal to FTS5 auxiliary rendering and does not alter public APIs or stored data.

### Verification

- `./testfixture ../ext/fts5/test/fts5aux.test` — 0 errors.
- `./testfixture ../ext/fts5/test/fts5af.test` — 0 errors.
- `./testfixture ../ext/fts5/test/fts5fault4.test` — 0 errors.
- `./testfixture ../ext/fts5/test/fts5fault9.test` — 0 errors.
- Fixed 15-pair benchmark — byte-identical dumps at all six sizes; 4,096-match cycle ratio 0.0769576.

I checked the relevant issues, comments, pull requests, and discussions; this pull request is not a duplicate.
