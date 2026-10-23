# M10 independent freeze-question closure

2026-10-23. Outcome: all six questions closed for contract freeze. No remaining
blocker from this scoped review. This is not implementation acceptance.

Exact reviewed design: DRAFT.md
`d3e65bf7624b56716ee9f6805d900f19c119f267bfec6ffbd35b119060fb5c9b`;
LEDGER.md `1554280166112044d815dc5bbd2660241298b28006bfab603b98a8351cf37fb7`;
PLANNING.sha256 `62e5b69e1a02e7a2fb0ea3c1fade4340b9c700e203f5a5393dc26abfa560c079`.
Complete bundle seal and all 172 source entries verified. Accepted source HEAD
remains `dee5f42acbb50e05d5690f1d2d13777c5c26d51d`.

1. **Overloads: closed.** DRAFT section 2, Exact operator and constructor table,
   lines 131-195, supplies shape/leaf domains, scalar/vector combinations,
   abstract-stage rules, exact component partitions, inference and excluded copy
   forms. Scalar control and increment/decrement remain explicit. Rechecked
   the dated WGSL arithmetic and shift rows rather than relying on old recollection.

2. **Update ordering: closed.** DRAFT section 3, lines 262-275, explicitly orders
   reference evaluation, old-value snapshot, RHS, operation and write. It also
   specifies OOB zero/discard and preservation of required later side effects.

3. **Selected outputs: closed.** DRAFT section 5, lines 576-607, supplies all six
   flag rows and zero/unselected module/count rules. Every mode private-emits with
   identical actual scratch/word appends. Measure is exact for unchanged source,
   options, capacities and loaded library, without a retained handle.

4. **Preflight/alias/padding: closed.** DRAFT section 5, lines 609-683, defines
   NULL and exact-size precedence before descriptor reads, ABI no-write behavior,
   selected-span checks, full-capacity pairwise disjoint caller preconditions,
   descriptor lifetime and record-padding initialization. Overlap is outside the
   contract, not a promised checked error. E11's initial draft wording is updated
   accordingly before tests or candidate inspection; no prior test is weakened.

5. **Declaration/target boundary: closed.** DRAFT lines 601-607 and LEDGER's
   target rows include every named struct/resource and its type dependencies
   regardless of use; all modes enforce target layout/format limits. A large
   fixed size alone is distinguished from an unencodable Offset/ArrayStride.
   Reflection-only cannot bypass these checks.

6. **Ledgers/profiles/ceilings: closed for freeze, actual audit pending.** DRAFT
   sections 6/8 and LEDGER provide exact private record/slice/lifetime declarations,
   compact compatibility profiles, checked target operation rows and explicit
   binary-only ABI4 CLI limits. JSON4 was not selected, so no new JSON schema is
   needed; old sidecar schemas remain. The lead approves proposed ceilings subject
   to actual implementation measurements. This closure does not certify future
   compiler frames, complete capacity proofs, historical preservation or emitted
   behavior; the independent obligations still require their evidence.

No candidate code was inspected or executed, no test/build was run, and no
compiler/repository file was changed. No exact future fixture/oracle was created
or disclosed. Only these independent evaluator documents are written.

High-level obligations are bound now. Concrete undisclosed sources may be
authored after the repository contract freeze, in parallel with implementation,
and must be sealed before candidate inspection/execution. No helper was spawned.
