# ISSUE-002 — FTS5: snippet rescans phrase instances

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

Root [S]: `fts5SnippetFunction()` scores candidates in an instance loop.
Each `fts5SnippetScore()` call scans every phrase instance, and sentence lookup restarts from its first boundary.

## Reach and impact

Reach [S]: FTS5 `snippet()` calls on rows with multiple phrase instances are affected.
Impact [S]: A column with `I` instances and `S` sentence boundaries can perform `O(I² + I*S)` lookup work.
Impact [N]: Representative instance counts, latency, allocation cost, and end-to-end query impact are not measured.

## Evidence

- [S] `ext/fts5/fts5_aux.c:383-392` — each score call enumerates all `nInst` instances.
- [S] `ext/fts5/fts5_aux.c:487-528` — each matching instance triggers scoring and a fresh sentence scan.
- [S] `ext/fts5/fts5_main.c:2410-2448` — the current internal instance cache is ordered by encoded position.
- [S] `ext/fts5/fts5.h:110-133` — the public `xInst()` contract does not promise enumeration order.
- [S] https://sqlite.org/src/raw/ext/fts5/fts5_aux.c?ci=trunk — canonical trunk retains these loops; checked 2026-08-15.

## Prior art

Coverage: local ledger IDs, titles, symptoms, root causes, and symbols; checked=2026-08-15.
Gaps: Fossil history and tickets, SQLite Forum, GitHub activity, and release notes were not searched.

Target fit: Undecided — representative measurements and external prior-art research are incomplete.

## Direction

Read each column's instances once, then score candidate windows with monotonic boundaries and per-phrase counts.
Advance sentence boundaries monotonically; explicitly preserve or establish position order inside the FTS5 owner.
Preserve the current candidates, weights, adjustment, first-winning tie rule, and error order.

## Bounds

- Preserve: output text, `1000/1` and `120/100` weights, ties, locale, corruption detection, OOM, and first error.
- Exclude: `bm25()`, tokenizer behavior, generic extension API changes, and unrelated FTS5 query execution.
- Cost: per-column state; the implementation must not promote internal `xInst()` ordering into a public guarantee.

## Verification

- `./testfixture ../ext/fts5/test/fts5af.test` → all existing snippet outputs remain unchanged.
- `./testfixture ../ext/fts5/test/fts5corrupt3.test` → corruption and error results remain unchanged.
- Increasing-instance benchmark → scoring work grows near-linearly and output matches the baseline byte-for-byte.

## Missing

- Representative baseline and candidate measurements with variance and a correctness guard.
- External prior-art coverage and current target fit.
- User-selected Mode and Target.

## Resume

Index: Benchmark snippet scoring
Next: Define and run a high-instance `snippet()` baseline on current `upstream/master`.
Done when: Repeated runs record exact schema, data, query, instance counts, latency, and variance.

## Performance evidence

Workload: Not established.
Baseline [N]: Not measured.
Candidate [N]: No correction is implemented or measured.
Guard [N]: Existing output and corruption tests have not run for a candidate.
Boundary [N]: CPU cost, allocations, realistic instance counts, and end-to-end impact remain unmeasured.
