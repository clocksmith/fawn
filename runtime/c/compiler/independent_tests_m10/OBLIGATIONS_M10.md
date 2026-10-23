# M10 independent evaluator obligations: high-level freeze

2026-10-23. High-level obligations are frozen against the design identities below.
This is not a frozen concrete test suite, candidate review, execution result,
implementation acceptance, or implementation authorization. Exact fixtures,
input bytes, expected results, and mutations will remain undisclosed to the
implementation authors until the relevant candidate is sealed. This document
states acceptance requirements, not a private representation design.

## Identity and authority

Accepted baseline: `dee5f42acbb50e05d5690f1d2d13777c5c26d51d` on
`experimental/c-migration-dot-20261023`. Planning revision 2 identities:
- DRAFT.md: `d3e65bf7624b56716ee9f6805d900f19c119f267bfec6ffbd35b119060fb5c9b`.
- LEDGER.md: `1554280166112044d815dc5bbd2660241298b28006bfab603b98a8351cf37fb7`.
- PLANNING.sha256: `62e5b69e1a02e7a2fb0ea3c1fade4340b9c700e203f5a5393dc26abfa560c079`.
The complete planning seal and all 172 source-manifest entries match at closure. Existing unrelated callback/lifecycle edits make the checkout
non-clean; they are not evaluator changes or compiler acceptance inputs.

Read AGENTS, GOALS, INTENT, the root/runtime/C/compiler charter chain,
component-charter protocol, runtime map, native-adapter style, architecture's
semantic/native boundary, migration and M9 acceptance/current reproduction
contracts, and the actual consumer/witness route. Only evaluator notes are
written in this pass. No candidate was inspected; no build, test,
compiler edit, installation, download, Git mutation, or device work occurred.

Normative anchor: [WGSL CRD, 21 October 2026](https://www.w3.org/TR/2026/CRD-WGSL-20261021/),
particularly sections 5; 6.1/6.2/6.5; 8.5/8.7-8.10; 9.2-9.3; 12.1/12.13;
13.3.4; 14.4; 15.1/15.7; 17.1/17.2. Target anchor:
[Khronos SPIR-V specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html),
with the contract selecting SPIR-V 1.3 and Vulkan 1.1, not the latest unified
specification's whole feature set. Preserve version/date and validator options
in future receipts. Historical accepted Doe policies supplement, not silently
rewrite, the dated language rules.

## Numbered acceptance obligations

1. **E01: Freeze before evaluating.** Bind the exact closed contract, header,
   unchanged legacy CLI schemas, selected profiles, implementation manifest, installed tools,
   historical imports, and evaluator source separately. Freeze independent
   cases/oracles/controls before first candidate inspection or execution.
   Preserve all failed observations. Track every disclosure and subsequent
   candidate/evaluator change; later repairs cannot inherit pre-disclosure
   blindness. A defect is repaired or remains a blocker, never relabeled as an
   exclusion after observation.

2. **E02: One semantic family, every public adapter.** Cover all admitted vector
   widths/leaf kinds, fixed arrays, nominal/nested structures, declarations,
   constants, locals, formals, returns and chained value/reference access.
   Derive acceptance/rejection from the closed overload table, not compiler
   diagnostics or retained Zig output. Include full source validation of dead
   code and unused declarations. Verify one admitted program has equivalent
   current output across adapters where projection/capacity permits. Preserve
   historical successes and name each specific old rejection intentionally lifted.

3. **E03: Identity, materialization and dependency resolution.** Separate nominal
   structure identity from equal shape; array count value from spelling/signedness;
   abstract inference from explicit concrete conversion; and const evaluation
   from runtime snapshots. Include mixed component inference, concrete contexts,
   runtime indexing, missing/extra constructor operands, shadowed type-generators,
   forward dependencies and mixed type/extent/attribute/constant cycles. Compact
   zero/repeat/composite values and shared materialization must not be flattened
   by element count or recursively cloned. Unsupported families remain explicit.

4. **E04: Observable order and ownership of values.** Establish independent
   side-effect traces for constructor/call operands, nested index expressions,
   ordinary expressions and updates. Simple assignment evaluates its destination
   before its RHS. Compound update must snapshot the old addressed value before
   evaluating its RHS. Preserve whole-object reads before replacement, nested
   by-value argument/result snapshots, immutable categories, repeated calls and
   loop-local temporary refresh. Helpers may change storage; cached loads and
   aggregate temporaries must not conceal those effects. Failed access must not
   erase required later source-side effects.

5. **E05: Independent byte layout.** Pack source-declared objects from a separate
   small schema and checked integer layout model, without consulting candidate
   reflection, production layout code, host structs, or emitted decorations.
   Then independently compare typed reflection, target memory layout and executed
   meaningful bytes. Exercise member order, internal/trailing padding, vector
   widths, arrays of nested objects, explicit size/alignment, direct roots and
   legal runtime tails. Preserve untouched logical siblings and outside-range
   sentinels without inventing values for padding bytes.

6. **E06: Address-space admission and actual extent.** Separate natural layout,
   uniform constraints, fixed size, minimum binding size, runtime-tail offset,
   stride and effective binding length. The same declaration must be usable
   locally/storage without being silently repacked for uniform. Cover recursive
   host-shareability, invalid tail placement/nesting, invalid attributes and
   arithmetic overflow. Empty/undersized logical interpreter bindings are safety
   probes, not legal physical binding evidence. Physical descriptor alignment,
   minimum ranges, writable overlap and races remain host/runtime obligations.

7. **E07: Access safety is observable before a memory operation.** Distinguish
   invalid constant indices from robust runtime indices, signedness, every
   nested access step, rvalue versus reference bases and read-only permissions.
   The instruction model must reject invalid pointer formation/dynamic extraction,
   not merely catch an eventual load. Check typed aggregate zero/discard behavior,
   separate resource identities and lengths, OOB index-producing loads used by
   other accesses, member/vector writes, and partial trailing ranges. A guard
   using the wrong binding or guarding after an unsafe operation must fail.

8. **E08: Independent arithmetic and vector safety.** Check admitted operations
   per lane with mixed safe/exceptional inputs, component ordering and result
   shapes. Preserve integer wrap/division/remainder/shift rules, abstract errors,
   stage distinctions, signed zero, conversions and bit reinterpretation.
   Derive exact integer/bit results independently; use independent rounded or
   interval floating reference methods, not the candidate's host libm. Record
   deterministic model policy separately from broader WGSL/device latitude.
   Inspect execution for invalid intermediates hidden by a final selection.

9. **E09: Actual target semantics and oracle sensitivity.** Execute serialized
   SPIR-V from a fresh C build with an independent fail-closed instruction engine.
   It must support the admitted composite, memory, function, control-flow and
   conversion operations generally, not source names or production IR tags.
   Independently check target types, decorations, branch/Phi/dominance, temporary
   lifetimes and runtime-tail queries. Use handwritten target controls and
   validator-valid wrong-layout, wrong-component, wrong-guard, wrong-snapshot and
   wrong-operation mutants. A validator rejection is not a semantic-oracle kill.
   Unknown instructions, malformed modules and execution limits never pass.

10. **E10: Native ABI4 outputs and discovery.** Use separately compiled C and C++
    consumers through source archive/shared-library routes. Check the final flag
    truth table, exact public sizes, every reflected field, deterministic graph
    ordering, all declarations, name ownership, reserved/irrelevant values and
    null/sentinel distinctions. Destroy source/workspace contents after success
    to establish output independence. Compare measure counts with normal compile;
    shortage in any selected span must leave every bulk span unchanged and return
    complete exact counts only when justified. Never confuse scratch lower bounds
    with final output requirements.

11. **E11: Preflight, bounded resources and isolation.** Probe each named pool and
    output span at zero/exact/short/overflow boundaries and the explicitly
    checked alignment/extent boundaries, without fabricated invalid pointers.
    Full-capacity spans and descriptors must remain pairwise disjoint; overlap
    violates the contract and has no guaranteed failure/no-write behavior. Do not
    assert out-of-contract overlap rejection. Apply redzones, reuse after every admitted failure, and
    concurrent independent workspaces. Preserve the required no-write ABI-error
    paths, failure diagnostics, errno and caller FP environment. Inject numeric
    environment failures before publication. Prove no hidden library allocation,
    capacity-derived VLA, unchecked target-ID map or capacity-driven C recursion.
    Query and compile must agree on checked byte/ID/alignment arithmetic.

12. **E12: Target format and host cost are separate limits.** Reach actual target
    instruction-width/ID/word boundaries with adequate unrelated budgets; do not
    mistake an earlier capacity failure for the requested boundary. Use compact
    independently represented stress inputs, bounded iterative traversal and
    isolated resource-limit probes, not huge flattened field/value arrays in
    the oracle. Bind record/frame/workspace/combined-stack evidence to reviewed
    ceilings. Record clean/incremental build and verification cost without a
    performance claim. A ceiling change requires an explicit contract amendment.

13. **E13: Legacy and CLI rollback.** Preserve all three old headers byte-for-byte,
    existing function signatures/statuses and old JSON contracts, with exactly
    the selected additive exports. Unrepresentable legacy reflection must fail
    before publication and name the source declaration, not reject a separate
    grammar. Exercise the actual --shader-path/--out consumer, selected sidecar
    modes (no new JSON4 option), no-sidecar allocation behavior, exact CLI profile, allocation/I/O
    failures, no-clobber and owned rollback. Keep partial/uncertain filesystem
    outcomes distinct from successful publication and crash-atomic guarantees.

14. **E14: Real witnesses, retained gates and honest claim scope.** Rerun accepted
    M9, M8 and older expectations through sealed, narrowly named adapters. Keep
    unchanged same98 source paths and 91 unique-source identities with separate
    admission/emission/target-validation/instruction/native/device columns.
    Exercise pressure, divergence and project unchanged, with independent grid,
    edge, dispatch-padding, input-length and numeric expectations. They establish
    only their vector/helper/resource routes, not nested arrays/structs or full
    WGSL. Additional admissions require evidence; skipped paths are not passes.
    Preserve TranslationInfo/cache/native-adoption separation and unrun physical
    Vulkan/Metal/D3D12, writable-overlap and race qualification.

## Oracle construction discipline

The layout oracle starts from evaluator-declared type shapes and raw little-endian
numeric bytes. The instruction engine separately reads emitted target types and
memory decorations. The acceptance assertion joins independent expectations to
actual observable memory; reflection is another output under test, not input to
packing. Numerical expectation code and target execution do not share arithmetic
implementations where that would compare an implementation with itself.

Aggregate values in the oracle use small immutable trees/DAGs, sparse typed-zero
objects and explicit traversal frames. Deep graphs and large extents are modeled
without recursion proportional to source depth or materialization proportional
to an array's declared element count. Actual finite byte stores are bounded by
selected exercised bindings. Resource-stress checks run separately from normal
semantic execution, with explicit reached-stage and inconclusive dispositions.

Sensitivity must be demonstrated at three layers: handwritten semantic controls
for the interpreter; wrong expected bytes/layout/numeric values rejected by the
assertion layer; and semantically incorrect but target-valid emitted mutants
rejected by execution. Mutants must be recorded with their validator status and
detected divergence. A mere count of negative test statuses proves none of these.

The future portable runner must regenerate fixtures and expectations from its
sealed source plus explicit checked repository imports and installed tools. It
must not require this private evidence directory, prebuilt modules, developer
passes, downloaded dependencies, a GPU or source-recognition shortcuts. Full and
focused stages need distinct claims. Preserve the original seal and append
post-disclosure corrections with explicit scope rather than rewriting history.

## Freeze interpretation and concrete-suite timing

The six original questions close in FREEZE_CLOSURE_M10.md. This high-level seal
retains the same acceptance scope while incorporating the closed choices: no
runtime overlap-detection promise, no JSON4 CLI option, identical private emission
for all selected output modes, and target checks for all declared named structs
and resources. The compact K/L aggregate-only pools and removed target-operands
pool are the exact revised ABI/ledger, not the superseded draft.

Actual implementation record/frame/combined-stack evidence is still required;
the designer's standalone record probe is not candidate qualification. The lead
has approved proposed ceilings subject to that audit. A repository contract
freeze must bind these exact design choices and this obligations seal. Any
material change requires explicit reconciliation before evaluation.

Concrete undisclosed fixtures, oracles and controls may be authored after the
repository contract freeze in parallel with implementation, but their source
seal must precede all candidate inspection/execution. No exact fixture is
created or disclosed by this closure. Original draft documents and preparation
record remain unchanged and are historical, not the governing obligations.
