# Issue, Comment, and Pull Request Format

## Authority

`AGENTS.md` owns project identity, fork intent, branch roles, repository rules, and approval scope.
`ISSUES.md` owns `Next finding ID` and provides the compact overview of every finding.
Each `issues/ISSUE-NNN.md` is authoritative for that finding's complete current state and evidence.
`skill-fork-contribution-tracking` owns the workflow connecting these files and upstream contribution work.
`skill-maintainer-communication` owns external research quality, tone, disclosure, and publication checks.
`skill-semantic-compression-3-0` owns meaning-preserving compression of tracking content.
Current upstream contribution guides, forms, and templates override the generic external shapes below.

- Investigate before drafting or implementing.
- Let the user choose Report or Pull request mode.
- Report mode permits issues and comments but no source implementation.
- Pull request mode authorizes only the implementation scope recorded for the finding.
- Apply the contribution decision tree below; never treat its order as publication authorization.
- Show the exact current external draft and target before publication.
- Publish externally only after the user approves that exact draft and target.
- Verify source claims against the current canonical upstream branch.
- Keep fork-only tracking content out of upstream contribution diffs.

## Evidence vocabulary

Use evidence labels at the claim they qualify:

- `[O]` Observed: reproduced behavior with command, version, environment, and result.
- `[S]` Source-proven: current control flow, API ownership, or deterministic data flow proves the claim.
- `[A]` Assumed: an unverified premise is required by the claim.
- `[N]` Not measured: performance, resource, frequency, or user impact lacks measurement.

Never use one entry-wide label to upgrade weaker claims.
Never convert `[S]` behavior into `[O]` impact.
Preserve exact paths, symbols, commands, outputs, URLs, revisions, dates, and external drafts.

## Finding IDs and duplicate prevention

- IDs use `ISSUE-NNN`, start at `ISSUE-001`, have at least three digits, and remain permanent.
- `Next finding ID: ISSUE-NNN` in `ISSUES.md` is the only allocator.
- Re-read the complete current `ISSUES.md` immediately before allocating.
- Search IDs, titles, symptoms, root causes, symbols, locations, and proposed owners across `issues/`.
- Read every plausible matching issue record completely.
- Update the existing record when it already owns the root cause.
- Allocate the current ID, create its issue file, add its index row, and increment the allocator in one change.
- Never reuse, renumber, or create subsystem-, status-, session-, or contribution-specific sequences.
- External numbers and URLs belong in `Location`; they never replace the internal ID.

Issue headings and filenames use:

```text
issues/ISSUE-NNN.md
# ISSUE-NNN — <area>: <specific problem>
```

## `ISSUES.md` projection

`ISSUES.md` provides overview, not the complete research record.
Active rows project ID, title, State, Mode, Target, Priority, `Resume/Index`, and Location.
Terminal rows project ID, title, State, Mode, Target, Priority, Disposition Outcome, and Location.

The issue file is authoritative when a row disagrees with it.
Correct both in the same task; never leave a known projection mismatch.
Move terminal findings to the terminal table without changing their ID or file.

## Issue record contract

Every issue file contains these fields in this order:

```text
State: Hold | Drafted | Implementing | Ready | Published | Closed | Rejected
Mode: Report | Pull request | Undecided
Target: New issue | Issue comment | New pull request | Pull request comment | Undecided
Location: <exact external URL | Not published.>
Priority: High | Medium | Low
Confidence: High | Medium | Low
Type: <correctness | reliability | performance | maintainability | API | UI | build | test | other exact family>
Created: <YYYY-MM-DD>
Updated: <YYYY-MM-DD>
Source: `upstream/<branch>@<commit>`
```

Every issue file contains these sections:

- `Root`: one cause and the behavior it owns.
- `Reach and impact`: affected callers, users, states, frequency, and honest impact boundary.
- `Evidence`: exact source, reproduction, history, command, result, contract, or external evidence.
- `Prior art`: search coverage, relevant candidates, classifications, gaps, and target fit.
- `Direction`: smallest complete correction or ownership change.
- `Bounds`: behavior to preserve, compatibility limits, adoption cost, and excluded scope.
- `Verification`: narrowest checks that prove the proposed or implemented contract.
- `Missing`: exact publication evidence still unresolved, or `None.`.
- `Resume`: one next action and one observable completion condition, or `None.` for terminal work.

Use conditional sections only when applicable:

- `Bug reproduction` for an observed user-visible bug.
- `Performance evidence` for latency, throughput, allocation, I/O, or resource claims.
- `Shared change pressure` for duplication or ownership findings.
- `API and compatibility` for public, persisted, protocol, or migration boundaries.
- `Implementation` while Pull request mode is Implementing, Ready, Published, or Closed.
- `Draft` when an exact external issue, comment, or pull request draft exists.
- `Disposition` when upstream resolves, rejects, supersedes, merges, or closes the contribution.

## Lifecycle states

### Hold

Use Hold while any required publication evidence is missing.
Source-only claims, incomplete prior-art research, stale currentness, unverified reach, and unmeasured impact remain Hold.
`Missing` names every publication blocker; `Resume` names the single next action.

### Drafted

Use Drafted only after research, Mode, Target, and the exact external Draft are complete.
Drafted does not authorize publication.
Report mode normally moves from Drafted to Published after approved publication.
Pull request mode normally moves from Drafted to Implementing.

### Implementing

Use Implementing only for user-selected Pull request mode while the bounded source change is in progress.
Record branch, base revision, scope, commit state, push state, and focused checks under `Implementation`.

### Ready

Use Ready when the source change is complete, verified, committed, pushed, and ready for an upstream pull request.
The exact pull request Draft and Target must already exist.
Ready does not authorize publication.

### Published

Use Published only after an observable external issue, comment, or pull request exists.
Record its exact URL in `Location` immediately.
Preserve the exact published text under `Draft` or label a distinct `Published text` block when publication changed it.

### Closed

Use Closed when the external contribution or underlying problem reaches an observable terminal outcome.
Record outcome and evidence under `Disposition`; preserve the ID, history, exact location, and published text.

### Rejected

Use Rejected when investigation disproves the root cause or shows that the correction costs more than the evidenced problem.
Record the rejection evidence and retain it to prevent rediscovery.

## Required research

Before drafting or implementing:

1. Read `AGENTS.md`, `FORMAT.md`, the complete current `ISSUES.md`, and the selected issue record.
2. Confirm that no issue record owns the same observable problem or root cause.
3. Apply the complete current research contract from `skill-maintainer-communication`.
4. Record canonical upstream revision, search coverage, gaps, material candidates, classification, and target fit.
5. Verify source claims against canonical upstream, not only a personal or contribution branch.
6. Reproduce user-visible bugs before claiming `[O]` behavior.
7. Measure representative workloads before claiming meaningful performance value.
8. Inspect callers, compatibility, persistence, lifecycle, failure modes, and verification seams.
9. Label every material claim `[O]`, `[S]`, `[A]`, or `[N]` at its point of use.
10. Apply the contribution decision tree within the user's selected Mode.

A search result is only a candidate target.
A matching symbol, subsystem, or symptom does not establish common ownership.
Sibling-project behavior is a research lead and never proves target-project behavior.
Unread or unavailable plausible prior art keeps the finding on Hold.

## Finding quality gates

A finding survives only when all applicable gates pass:

- Current pain, risk, inconsistency, or repeated maintenance pressure is evidence-backed.
- One coherent root cause explains the reported behavior.
- Affected callers, users, states, or workflows are bounded and reachable.
- Existing capability does not already solve the problem.
- The proposed direction is smaller than the work, risk, or ambiguity it removes.
- Compatibility, migration, persistence, lifecycle, and verification costs are explicit.
- One focused observable check can verify the proposed contract.
- Source evidence is not inflated into observed user harm.

Reject findings based only on aesthetics, names, comments, TODOs, clone output, or generic best practice.
Reject broad cleanup, architecture campaigns, unmeasured performance claims, and speculative future use.
Use one issue record per independent root cause.

## Conditional evidence

### Bug reproduction

Observed bug claims require:

```text
Environment: <version, platform, deployment, relevant configuration>
Reproduction: <minimal deterministic steps>
Actual [O]: <observed result>
Expected: <contract-backed expected result>
```

Do not use a bug form for source-only maintainability concerns without reproduced behavior.

### Performance evidence

Meaningful performance claims require:

```text
Workload: <representative input and environment>
Baseline [O]: <command, measurements, and variance>
Candidate [O]: <command, measurements, and variance>
Guard [O]: <correctness-equivalence result>
Boundary [N|O]: <end-to-end impact and what remains unmeasured>
```

No representative measurement means no meaningful performance claim.

### Shared change pressure

Duplication or ownership findings require:

```text
Copies [S]: <count and exact live owners>
Pressure [S]: <one realistic reason the copies change together>
Drift [O|S|N]: <current divergence or honest absence>
Owner: <smallest coherent proposed owner>
Cost: <coupling and abstraction boundary>
```

Clone detectors, AST matches, text similarity, and shared names produce candidates only.
Framework wiring, DTO mirrors, provider boundaries, fixtures, migrations, generated code, and idioms are not findings by default.
Consolidation must be simpler than synchronized explicit code.

### API and compatibility

Public, persisted, protocol, or migration changes require:

```text
Callers [S]: <affected consumers>
Contract [S|O]: <current invariant>
Compatibility: <behavior that must remain>
Migration: <required action or None.>
```

## Contribution decision and target selection

Apply this decision tree:

1. Recommend a focused pull request when its implementation gate passes.
2. Otherwise recommend a comment when an existing thread owns the same problem or root cause.
3. Otherwise recommend a new issue when durable maintainer discussion is useful.
4. Otherwise keep the finding on Hold.

The decision recommends a shape; the user still selects Mode and approves publication.

### Focused pull request

The implementation gate requires:

- Pull request mode selected by the user;
- verified root cause, callers, failure modes, compatibility boundary, and bounded fix;
- no active implementation already owning the correction;
- focused checks covering every changed contract; and
- a contribution diff without fork-only tracking content.

A pull request may resolve or materially advance an existing issue without a duplicate issue.

### Existing issue comment

Comment only when the thread owns the same problem or root cause and new evidence advances diagnosis or resolution.
A useful comment adds a fact, reproduction, source anchor, measurement, verified fix, or necessary scoped question.
Similar symptoms alone do not justify a comment.
Use a distinct record for a distinct root cause.

### Existing pull request comment

Comment only when the current diff changes the exact lifecycle, function, invariant, or owner involved.
Add actionable evidence and state when it does not request scope expansion.
Never redirect an unrelated pull request.

### New issue

Open a new issue only when no thread owns the same problem or root cause and durable discussion is useful.
Use one issue per independent root cause.

### Hold without publication

Keep Hold while currentness, reach, prior art, impact, target, or correction value remains unresolved.
State the exact gap in `Missing` and the next bounded action in `Resume`.

## Resume contract

Every nonterminal record contains one index projection and one current continuation action:

```text
Index: <2–6 word action summary>
Next: <single bounded action>
Done when: <observable evidence that completes it>
```

`ISSUES.md/Next` must equal `Resume/Index` exactly.
Replace `Resume` after completing the action; do not accumulate a task log.
Re-check the source and external target before relying on a stale Resume action.
Terminal records use `Index: —`, `Next: None.`, and `Done when: None.`.

## Draft contract

Apply the upstream form or template first and map the generic content below into its fields.
Apply `skill-maintainer-communication` to every external draft.
Keep exact drafts literal; semantic compression applies only to their supporting ledger context.
Any Draft or Target change requires showing the complete current draft and target again before publication.

### New issue

```markdown
## Summary

<One concise paragraph describing one root cause and affected behavior.>

## Evidence

- `<stable source link or path:line>`: <specific evidence>.
- <Reproduction, command result, history, or relevant external link>.

## Impact

<Observed impact or an explicit source-proven or not-measured boundary.>

## Proposed direction

<Smallest complete correction without unrelated cleanup.>

## Risks and boundaries

<Behavior to preserve and material compatibility or adoption constraints.>

## Verification

- <Focused existing test, reproduction, benchmark, or observable check>.

## Question

<One concrete maintainer decision or confirmation request.>

I checked the relevant issues, comments, pull requests, and discussions; this report is not a duplicate.

## Involvement

I am reporting this finding only and am not currently proposing a pull request.
```

### New pull request

```markdown
## Summary

<Root cause and scoped correction.>

## Evidence

- `<stable source link or path:line>`: <source or reproduced behavior establishing the problem>.
- <Relevant issue, pull request, benchmark, command result, or API contract>.

## Changes

- <Concrete behavior or ownership change>.
- <Important behavior intentionally left unchanged>.

## Risks and boundaries

- <Compatibility, persistence, lifecycle, UI, or provider boundary>.
- <Why this avoids broader cleanup or abstraction>.

## Verification

- `<focused command or scenario>` — <observed result>.

I checked the relevant issues, comments, pull requests, and discussions; this pull request is not a duplicate.
```

### Existing issue comment

```markdown
Hi, thanks for documenting this.

I noticed one additional detail in the current implementation:

- `<stable source link or path:line>`: <specific evidence>.
- <Observed or source-proven consequence>.

This appears to share the issue's root cause because <precise ownership link>.

Would it make sense to <one scoped question or direction>?

I checked the relevant discussion; this evidence is not already reported.

I am reporting this finding only and am not currently proposing a pull request.
```

### Existing pull request comment

```markdown
Hi, thanks for working on this.

I noticed one edge in the current diff:

- `<stable source link or diff hunk>`: <specific evidence>.
- <Observed or source-proven consequence>.

This does not require expanding the current scope unless it is part of the same invariant.

Would it make sense to <one concrete question>?

I checked the relevant discussion; this evidence is not already reported.
```

## Implementation contract

Pull request records use:

```text
Branch: <contribution branch>
Base: `upstream/<branch>@<commit>`
Scope: <authorized source change>
Commit: <SHA | Pending.>
Push: <fork branch | Pending.>
Checks:
- `<focused command>` → <observed result>
```

Record durable results, not raw work logs.
Every listed check must prove a changed observable contract.
Ready requires no pending implementation, commit, push, verification, or exact-draft item.

## Disposition contract

Closed and Rejected records use:

```text
Outcome: Merged | Fixed | Duplicate | Declined | Superseded | Invalid | Withdrawn | Other exact outcome
Evidence: <external URL, commit, release, or maintainer statement>
Checked: <YYYY-MM-DD>
```

Do not infer resolution from inactivity, branch deletion, or a closed state without reading the final thread and linked work.

## Semantic compression

- Store project-wide facts once in `AGENTS.md`.
- Store workflow definitions once in `FORMAT.md`.
- Store complete finding-specific facts once in its issue record.
- Keep `ISSUES.md` to the allocator and compact projection.
- Use stable labels and ordering; avoid aliases beyond the defined evidence markers.
- Use fragments only when agency, scope, condition, and evidence remain unambiguous.
- Remove raw search output, repeated summaries, activity logs, and duplicated conclusions.
- Preserve exact source anchors, commands, results, URLs, revisions, dates, external forms, drafts, and published text.
- Omit inapplicable conditional sections; never omit applicable evidence required by a gate.
- Compression never resolves a contradiction or upgrades evidence.

## Ledger validation

Run the read-only validator bundled with `skill-fork-contribution-tracking` after every ledger mutation.
It checks allocator continuity, unique IDs, index-to-record links, projection equality, and lifecycle-required sections.
It also rejects year-based IDs, stale `Next` projections, missing published locations, and misplaced terminal records.
It never edits files or claims that evidence, prior art, target fit, or publication value is true.

## Publication gate

Before publishing, verify every item:

- The permanent ID and issue file exist.
- `ISSUES.md` matches the issue file's State, Mode, Target, Priority, Next projection, and Location.
- The user selected Report or Pull request mode.
- Current upstream still contains the relevant behavior.
- Every plausible prior-art candidate was read fully.
- The selected target owns the same root cause.
- Evidence labels match the actual proof.
- Claims preserve observed, source-proven, assumed, and unmeasured boundaries.
- The draft contains one root cause and follows the current upstream form.
- Pull request mode is Ready and its contribution diff excludes fork-only tracking files.
- The duplicate-search statement is truthful.
- The user approved the exact current draft and exact target.
- Location and State will be updated immediately after publication.

## Prohibited actions

- Never publish without approval of the exact current draft and target.
- Never choose Report or Pull request mode for the user.
- Never implement while Mode is Report or Undecided.
- Never publish a pull request before Ready.
- Never treat source text, clone output, a TODO, or a search hit as sufficient evidence.
- Never inflate maintenance risk or source invariants into observed user harm.
- Never split one root cause across multiple IDs.
- Never combine independent root causes in one contribution.
- Never expose internal Priority, Confidence, local paths, or adversarial notes externally.
- Never include `AGENTS.md`, `FORMAT.md`, `ISSUES.md`, or `issues/` in an upstream contribution diff.
