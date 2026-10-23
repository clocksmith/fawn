# C migration work

Read the repository AGENTS.md, applicable CATSCAN.md, and [common style](STYLE.md).
C is selected. Author coherent subsystems in parallel; integrate focused-checked
source progress into canonical main without force, preserving production defaults.
review and run focused compile/behavior checks as code changes. Do not require a
new exhaustive independent qualification campaign before implementing the next
subsystem. Integrated independent qualification still precedes qualified cutover.
Label authored, compiled, exercised, accepted and hardware-qualified separately.

- Run `make -C runtime/c check` before committing: mechanical style
  (`tools/check_style.py`), tool tests and a build of every owner. Each owner's own
  `check` remains its behavioral gate; this one catches cross-owner breakage.
- Every C/C++/Objective-C source and header is at most 999 physical lines.
  Split meaningful responsibilities; never pack lines or hide generated complexity.
- A changed public header, enum or status set builds and checks every in-tree
  consumer in the same commit. New or changed status conversions switch
  exhaustively (no `default:` hiding a new value), so `-Werror=switch` names each
  consumer to update.
- One rule, representation and lifetime owner. Reuse existing typed operations
  rather than parallel parsers, classifiers, queues or reference counts.
- Remove superseded helpers and obsolete live guidance when replacing them;
  preserve behavioral tests and recoverability through Git. Historical sealed
  evidence is not live implementation and must not be rewritten.
- Keep the common guide authoritative. Add local guidance only for genuine
  domain-specific invariants, not copied general instructions.
- Coordinate disjoint paths. Shared interfaces/build edits have one named owner.
  No main/default switch or physical-backend claim from authored C or host tests.
