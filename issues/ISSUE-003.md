# ISSUE-003 — FTS5: highlighting rescans output prefixes

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

Coverage: local ledger IDs, titles, symptoms, root causes, and symbols; checked=2026-08-15.
Gaps: Fossil history and tickets, SQLite Forum, GitHub activity, and release notes were not searched.

Target fit: Undecided — representative measurements and external prior-art research are incomplete.

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
- External prior-art coverage and current target fit.
- User-selected Mode and Target.

## Resume

Index: Benchmark highlight appends
Next: Define and run a high-match `highlight()` baseline on current `upstream/master`.
Done when: Repeated runs record exact schema, data, query, append counts, output bytes, latency, and variance.

## Performance evidence

Workload: Not established.
Baseline [N]: Not measured.
Candidate [N]: No correction is implemented or measured.
Guard [N]: Existing highlight and snippet output tests have not run for a candidate.
Boundary [N]: Prefix-scan cost, allocations, realistic output sizes, and end-to-end impact remain unmeasured.
