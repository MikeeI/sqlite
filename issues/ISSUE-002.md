# ISSUE-002 — FTS5: snippet rescans phrase instances

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

Coverage: SQLite Bug Forum, SQLite User Forum, canonical Fossil history, and `sqlite/sqlite` GitHub activity; checked 2026-08-16.

- Distinct: https://sqlite.org/forum/forumpost/f78be6bf98583449ff0f3d322dc600c62e94b3735aa57dc521b32d6c968a72d8 — multiple-snippet feature question, not rescanning cost.
- Distinct: https://sqlite.org/forum/forumpost/06d073099dd312a94f1938c292b22e6e8b426d76e579cd73edb63ed898eaa68f — general FTS design guidance.
- Related: https://sqlite.org/src/info/60de159476edbd48dc363f7f77f09c32ea68422f — sentence-biased scoring history.

Gaps: No exhaustive absence claim; measurements and target fit remain unresolved.

Target fit: User selected a proof-of-concept New pull request. External publication remains contingent on benchmark and candidate evidence, exact user approval of the draft and target, and SQLite's submission path.

## Direction

Read each column's instances once, then score candidate windows with monotonic boundaries and per-phrase counts.
Advance sentence boundaries monotonically; explicitly preserve or establish position order inside the FTS5 owner.
Preserve the current candidates, weights, adjustment, first-winning tie rule, and error order.

## Bounds

- Preserve: output text, `1000/1` and `120/100` weights, ties, locale, corruption detection, OOM, and first error.
- Exclude: `bm25()`, tokenizer behavior, generic FTS5 APIs, unrelated FTS5 query execution, and tracking files.
- Cost: per-column state; the implementation must not promote internal `xInst()` ordering into a public guarantee.

## Verification

- `./testfixture ../ext/fts5/test/fts5af.test` → all existing snippet outputs remain unchanged.
- `./testfixture ../ext/fts5/test/fts5corrupt3.test` → corruption and error results remain unchanged.
- Increasing-instance benchmark → scoring work grows near-linearly and output matches the baseline byte-for-byte.

## Missing

- Pending: baseline benchmark, frozen evaluator, and deterministic correctness guard.
- Pending: bounded candidate correction and measurements with variance.
- Pending: focused checks, commit, push, and exact pull request draft.
- Pending: SQLite submission agreement or other accepted submission path; external publication requires evidence and exact user approval.

## Resume

Index: Build snippet harness
Next: Build and freeze the documented evaluator on the contribution base.
Done when: The baseline build emits deterministic guard output and the fixed measurement schema.

## Performance evidence

Workload: Synthetic evaluator defined below; real-world representativeness is not established.
Baseline [N]: Not measured.
Candidate [N]: No correction is implemented or measured.
Guard [N]: Existing output and corruption tests have not run for a candidate.
Boundary [N]: CPU cost, allocations, realistic instance counts, and end-to-end impact remain unmeasured.

### Benchmark plan

Baseline: current Source commit; candidate: one isolated same-base correction.
Environment: Ubuntu 24.04.4, AMD 5950X, GCC 13.3.0, optimized build, and CPU 4.
Workload: `fts5(body,detail=full)` with one deterministic document containing `alpha. ` repeated `I` times.
Query: `MATCH 'alpha'` with `snippet(ft,0,'[',']','...',32)`; `I=128,256,512,1024,2048,4096`.
Timing: setup and prepare are untimed; primary metric is cycles per complete call at `I=4096`.
Protocol: 3 warmups; fixed baseline-calibrated repetitions lasting >=250ms; 15 AB/BA pairs; median ratio and MAD.
Secondary metrics: instructions, branches, elapsed time, and heap.
Theory [S]: source inspection predicts repeated scan work; measurements are required before any publication claim.
Reportability [N]: require >=50% cycle reduction, scaling exponent <=1.25, and heap O(I) at <=10% above baseline.
Correctness guardrails: normal results must be byte-identical and preserve first-winning ties across multiple phrases and columns, equal scores, sentence starts, token limits `0/1/32/64`, and locale/trigram cases.
Failure guardrails: preserve corruption, OOM, and first-error behavior; include `fts5af` and `fts5corrupt3`.
Stop: do not pursue publication if baseline scaling is effectively linear or realistic cardinality is not costly.
