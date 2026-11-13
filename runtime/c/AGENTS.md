# C migration work

Read the repository AGENTS.md, applicable CATSCAN.md, and [common style](STYLE.md).
C is selected. Author coherent subsystems in parallel; integrate focused-checked
source progress into canonical main without force, preserving production defaults.
review and run focused compile/behavior checks as code changes. Do not require a
new exhaustive independent qualification campaign before implementing the next
subsystem. Integrated independent qualification still precedes qualified cutover.
Label authored, compiled, exercised, accepted and hardware-qualified separately.

- For ordinary implementation edits, run `make -C runtime/c check-style` and
  the affected owner's behavioral target (`check` or the relevant `test-*`
  target in its Makefile). Once those pass, commit and continue; there is no
  all-owner build requirement for an owner-private edit or a prose-only change.
- Run `make -C runtime/c check-tools` when changing the Python build/check tools.
  Shared headers, enums/status sets, cross-owner build rules or process-global
  ownership changes require `make -C runtime/c check`: style, tool tests,
  platform checks, every owner's build and the mutable-globals check. Keep this
  aggregate check for integrated runtime release qualification as well.
  Owner behavioral and applicable lifecycle/backend gates remain required;
  an aggregate build is not a substitute for them.
- Owner Makefiles default every scratch tree under `$(DOE_C_TMP)` from
  [build-root.mk](build-root.mk), one root per checkout. Clones on one host
  never share objects. The style check's `tmp` rule rejects fixed `/tmp` defaults.
- Every C/C++/Objective-C source and header is at most 999 physical lines.
  Split meaningful responsibilities; never pack lines or hide generated complexity.
- A changed public header, enum or status set builds and checks every in-tree
  consumer in the same commit, including the software-Vulkan resource lifetime
  clients (`make -C runtime/c check-vulkan` with a CPU ICD), which no owner `check`
  runs. New or changed status conversions switch
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
