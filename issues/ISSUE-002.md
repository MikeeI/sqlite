# ISSUE-002 — FTS5: snippet rescans phrase instances

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

Root [S]: `fts5SnippetFunction()` scores candidates in an instance loop.
Each `fts5SnippetScore()` call scans every phrase instance, and sentence lookup restarts from its first boundary.

## Reach and impact

Reach [S]: FTS5 `snippet()` calls on rows with multiple phrase instances are affected.
Impact [S]: A column with `I` instances and `S` sentence boundaries can perform `O(I² + I*S)` lookup work.
Impact [O]: At 4,096 instances, the candidate used 1.63% of baseline cycles in the synthetic evaluator.

## Evidence

- [S] `ext/fts5/fts5_aux.c:383-392` — each score call enumerates all `nInst` instances.
- [S] `ext/fts5/fts5_aux.c:487-528` — each matching instance triggers scoring and a fresh sentence scan.
- [S] `ext/fts5/fts5_main.c:2410-2448` — the current internal instance cache is ordered by encoded position.
- [S] `ext/fts5/fts5.h:110-133` — the public `xInst()` contract does not promise enumeration order.
- [S] https://sqlite.org/src/raw/ext/fts5/fts5_aux.c?ci=trunk — canonical trunk retains these loops; checked 2026-08-15.

## Prior art

Coverage: SQLite forums, canonical Fossil history, and `sqlite/sqlite` GitHub activity were searched.
The search was checked on 2026-08-16.

- Distinct: https://sqlite.org/forum/forumpost/f78be6bf98583449ff0f3d322dc600c62e94b3735aa57dc521b32d6c968a72d8 — multiple-snippet feature question, not rescanning cost.
- Distinct: https://sqlite.org/forum/forumpost/06d073099dd312a94f1938c292b22e6e8b426d76e579cd73edb63ed898eaa68f — general FTS design guidance.
- Related: https://sqlite.org/src/info/60de159476edbd48dc363f7f77f09c32ea68422f — sentence-biased scoring history.

Gaps: Coverage does not claim exhaustive absence; representative workload frequency remains unresolved.

Target fit: The user selected a proof-of-concept New pull request.
Publication requires approval of the exact target and draft plus SQLite's accepted submission path.

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

- External publication awaits exact user approval and SQLite's accepted submission path.

## Resume

Index: Approve exact pull request
Next: Obtain approval for the exact target and draft below.
Done when: The approved pull request is published and its URL is recorded.

## Implementation

Branch: `perf/fts5-snippet-scoring`
Base: `upstream/master@f17a2ee06b2ba5e65528aa3e0e3f2508c75987fb`
Scope: Eliminate repeated snippet-candidate rescans while preserving exact scoring and output.
Commit: `4401094cca`
Push: `origin/perf/fts5-snippet-scoring`
Checks:
- `./testfixture ../ext/fts5/test/fts5af.test` → 0 errors.
- `./testfixture ../ext/fts5/test/fts5corrupt3.test` → 0 errors.
- Fixed 15-pair benchmark → byte-identical dumps at all six instance counts.
- Contribution diff against the base contains only `ext/fts5/fts5_aux.c`.

## Performance evidence

Workload: Synthetic repeated-match evaluator; real-world representativeness is not established.
Baseline [O]: Median cycle scaling across 128–4,096 instances had exponent 1.928422.
Candidate [O]: Median cycle scaling exponent was 0.977575.
Candidate [O]: At 4,096 instances, the median cycle ratio was 0.0163015 with MAD 0.00007627.
Guard [O]: Dumps were byte-identical at every size; focused allocation-failure and corruption tests passed.
Boundary [N]: Representative instance counts, allocations, and end-to-end query impact remain unmeasured.

### Benchmark plan

Baseline: current Source commit; candidate: one isolated same-base correction.
Environment: Ubuntu 24.04.4, AMD 5950X, GCC 13.3.0, optimized build, and CPU 4.
Workload: `fts5(body,detail=full)` with one deterministic document containing `alpha. ` repeated `I` times.
Query: `MATCH 'alpha'` with `snippet(ft,0,'[',']','...',32)`; `I=128,256,512,1024,2048,4096`.
Timing: setup and prepare are untimed; primary metric is cycles per complete call at `I=4096`.
Protocol: 3 warmups; fixed baseline-calibrated repetitions lasting >=250ms; 15 AB/BA pairs; median ratio and MAD.
Secondary metrics: instructions, branches, elapsed time, and heap.
Theory [S]: source inspection predicts repeated scan work; the fixed evaluator confirmed superlinear baseline scaling.
Reportability [O]: cycle reduction exceeded 50%, scaling exponent was at most 1.25, and heap stayed within baseline.
Correctness guardrails preserve first-winning ties across phrases, columns, equal scores, and sentence starts.
They also preserve token limits `0/1/32/64` plus locale and trigram cases.
Failure guardrails: preserve corruption, OOM, and first-error behavior; include `fts5af` and `fts5corrupt3`.
Stop: do not pursue publication if baseline scaling is effectively linear or realistic cardinality is not costly.

## Draft

Target: `sqlite/sqlite` — New pull request from `MikeeI:perf/fts5-snippet-scoring`

Title: Avoid rescanning FTS5 snippet candidates

Body:

### Summary

`fts5SnippetFunction()` rescans all phrase instances while scoring each candidate window.
This change scores candidates with monotonic per-column state while preserving snippet selection and rendering.

### Evidence

- `ext/fts5/fts5_aux.c:fts5SnippetScore()` enumerated every instance for every candidate.
- A fixed synthetic evaluator measured a baseline cycle-scaling exponent of 1.928422.
- The candidate exponent was 0.977575; at 4,096 instances its median cycle ratio was 0.0163015.
- Reproducible evaluator: https://github.com/MikeeI/sqlite/tree/personal/issues/evidence/ISSUE-002

### Changes

- Maintain monotonic per-column instance and sentence state while evaluating candidate windows.
- Preserve candidate boundaries, score weights, adjustment, first-winning ties, errors, and output bytes.

### Risks and boundaries

- Internal instance order remains an implementation detail rather than a public `xInst()` contract.
- The change does not alter public FTS5 APIs, tokenization, `bm25()`, query execution, or stored data.

### Verification

- `./testfixture ../ext/fts5/test/fts5af.test` — 0 errors.
- `./testfixture ../ext/fts5/test/fts5corrupt3.test` — 0 errors.
- Fixed 15-pair benchmark — byte-identical dumps at all six sizes; 4,096-instance cycle ratio 0.0163015.

I checked the relevant issues, comments, pull requests, and discussions; this pull request is not a duplicate.
