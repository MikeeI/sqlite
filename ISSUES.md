# Issue and Pull Request Tracking

Read this index at the start of every agent session before repository work.
`FORMAT.md` owns research, lifecycle, drafting, implementation, and publication rules.
Each linked `issues/ISSUE-NNN.md` is the complete authoritative record for one root cause.
This file owns `Next finding ID` and projects current issue-file state.
`Next` is a 2–6 word projection of the issue record's `Resume/Next`.
When a row disagrees with its issue file, correct the row from the issue file in the same task.

Next finding ID: ISSUE-008

## Active

| ID | Finding | State | Mode | Target | Priority | Next | Location |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [ISSUE-001](issues/ISSUE-001.md) | window: first_value retains partition rows | Implementing | Pull request | New pull request | High | Implement retention patch | Not published. |
| [ISSUE-002](issues/ISSUE-002.md) | FTS5: snippet rescans phrase instances | Implementing | Pull request | New pull request | High | Implement scoring patch | Not published. |
| [ISSUE-003](issues/ISSUE-003.md) | FTS5: highlighting rescans output prefixes | Implementing | Pull request | New pull request | High | Implement builder patch | Not published. |
| [ISSUE-004](issues/ISSUE-004.md) | vtab: busy close rolls back state | Hold | Undecided | Undecided | High | Probe private-vtab busy | Not published. |
| [ISSUE-005](issues/ISSUE-005.md) | win: timed SHM lock skips completion collection | Hold | Undecided | Undecided | High | Reproduce timed SHM race | Not published. |
| [ISSUE-006](issues/ISSUE-006.md) | CLI .restore: missing source can clear target | Hold | Undecided | Undecided | High | Review restore prior art | Not published. |
| [ISSUE-007](issues/ISSUE-007.md) | sqldiff: changeset target opens before validation | Hold | Undecided | Undecided | High | Review exact-root prior art | Not published. |

## Terminal

| ID | Finding | State | Mode | Target | Priority | Outcome | Location |
| --- | --- | --- | --- | --- | --- | --- | --- |
