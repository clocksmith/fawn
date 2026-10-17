# Doe Zig style guide

This guide is the style contract for owned code under `runtime/zig/`. Use the
Zig version pinned in [`../../config/toolchains.json`](../../config/toolchains.json).
Component intent comes from the applicable `CATSCAN.md` chain; source ownership
and size policy come from [`source-layout.json`](source-layout.json).

Readable code makes its inputs, decisions, ownership, and failure paths
visible. Shorter is better only when it removes repetition or indirection.

## Core principles

- Prefer explicit typed contracts over inferred behavior.
- Keep runtime decisions deterministic and traceable.
- Fail fast on invalid/unsupported inputs with actionable errors stating what
  was expected and what was received.
- Keep hot-path execution allocation-light after initialization.
- Give every semantic fact one authoritative owner and every subsystem one
  narrow public surface.
- Eliminate duplicate registries, policy maps, profile unions, errors, artifact
  fields, and conversion taxonomies before optimizing file count.

## Restricted implementation style

Use simple data structures, explicit state machines, functions, and narrow
interfaces. Language features earn their place by making an invariant easier
to inspect or enforce; terseness and generic reuse alone are insufficient.

- Prefer concrete request, result, state, and owner types. `anytype` is for
  small private generic mechanics; it must not hide a subsystem's dependencies,
  failure contract, or ownership. A generic adapter names its required interface.
- Keep `comptime` local and inspectable: completeness checks, typed tables, and
  narrow adapter binding are appropriate. Avoid recursive type machinery,
  generated object frameworks, and implicit feature discovery. Required
  operations cannot become optional through reflection or a catch-all default.
- Dispatch over closed instruction, command, and lifecycle sets exhaustively.
  Name deliberately unsupported cases. A new tag must require a disposition;
  reserve catch-all handling for genuinely open input domains or an explicitly
  shared policy that cannot silently grant success.
- Use checked arithmetic for sizes, offsets, counts, and allocation products
  before casts or native calls, including optimized builds. Wrapping arithmetic
  is confined to algorithms whose contract requires modular behavior.
- Keep allocation, ownership transfer, and failure visible at the call site.
  Follow [memory](#memory) and [errors and diagnostics](#errors-and-diagnostics);
  compile-time structure does not prove lifetime safety.

Small generic helpers and function-pointer interfaces are useful when they
remove repetition without moving policy or hiding resource lifetimes. Do not
introduce a general framework where a concrete function or tagged state suffices.

## Invariant registry

These identifiers are normative; reviews, regression tests, and receipts name
them when a change touches the corresponding boundary.

| ID | Invariant | Enforcement surface |
| --- | --- | --- |
| `INV-OWNER-001` | Shared behavior lives with the narrowest subsystem that owns its semantics; dependency direction remains one-way. | `tools/check_source_layout.py`, `tools/check_core_import_fence.py`, import tests |
| `INV-PLAN-002` | Command-oriented immediate, recorded, replayed, direct, and indirect execution share the prepared-operation contract; ordinary WebGPU and package programs share applicable semantics without a universal interpreter. | affected executor parity tests, trace/replay gates |
| `INV-RESOURCE-003` | Every resource has one explicit owner and every ownership transition has one cleanup path. | allocator-backed tests, lifecycle and failure-path tests |
| `INV-RECEIPT-004` | Structural refactors preserve the canonical behavior receipt and first failure boundary. | trace determinism, replay, semantic digest, and workload-oracle tests |
| `INV-FACADE-005` | Module roots and compatibility facades aggregate, normalize, delegate, or translate errors; they do not own independent domain behavior. | `source-layout.json`, `tools/check_source_layout.py`, exercised facade tests |
| `INV-REGISTRY-006` | Each command, capability, error class, artifact identity, and timing field has one authoritative registry or contract. | compile-time coverage, schema gates, duplicate-declaration inventory |
| `INV-CONTEXT-007` | Promoted execution boundaries use explicit context, request, and report types rather than `anytype` dependency hiding. | affected-path compile and parity tests |
| `INV-ARCH-008` | Every production module has one declared owner, layer, and special role, with zero forbidden dependency edges or cycles. | versioned `source-layout.json`, architecture graph gate |
| `INV-PROVIDER-009` | Provider selection has one ordinary runtime construction root; FFI provider integrations are explicitly enumerated, and provider configuration/state is instance-owned. | `tools/check_core_import_fence.py`, provider construction and concurrent-session tests |
| `INV-BORROW-010` | A borrowed prepared operation is valid only for synchronous execution/callback scope; queued or retained work owns a deep immutable snapshot. | `OwnedPreparedOperation` tests, observer and executor lifetime tests |
| `INV-PORT-011` | Capability ports and provider drivers expose domain-specific operations; no broad command escape hatch survives in the adapter contract. | `tools/check_core_import_fence.py`, provider adapter compilation tests |
| `INV-LIVE-012` | Re-export reachability is not evidence of use. Incubation modules require a real consumer or are removed from production roots. | architecture reachability reports, consumer search, affected package tests |

No prose-only waiver satisfies an invariant. If the named global gate cannot
observe a changed behavior, the affected subsystem adds a focused
characterization or parity test in the same change.

## Build decisions and observed costs

- Build decisions are immutable typed values from versioned policy, rejecting
  unsupported versions, missing fields, and incompatible selections before
  consumers compile. Device facts resolve once per device; changing invocation
  inputs keep explicit validation. Algorithm constants stay with the algorithm.
- Derive diagnostic identity and coverage metadata from the owning contract.
  Compile-time completeness is only structural evidence; validators, executors,
  and resource lifetimes need behavioral tests.
- Declare steady-execution allocation sites, distinguishing reserved capacity
  from allocator calls. Every cache specifies identity, owner, capacity,
  invalidation, and full-capacity behavior. Owning structs cannot be freely
  copied; Zig has no general ownership checker.
- Keep pointers, casts, native handles, and foreign callbacks at narrow
  adapters. Required metadata and allocation failures stay errors; API
  admission and checked arithmetic work in every supported optimization mode.
- Instrument the real execution path with bounded optional observation,
  explicit unavailable values, and overflow reporting; confirm benefit in the
  ordinary configuration. Telemetry snapshots read existing state; export,
  file output, and configuration reads happen at an explicit fallible
  collection boundary outside the measured operation. Deferred capture owns
  its inputs until publication or cleanup.
- Specialization, forced inlining, and vector variants need application,
  binary-size, and build evidence before adoption.
- Content hashes need actual artifact bytes. Versioned evidence keeps derived
  identifiers, observed source hashes, and unobserved compiler stages distinct;
  a configured tool is no evidence it ran.
- Elapsed-time measurements use an elapsed clock, not calendar time; keep
  unavailable timing explicit, never a legacy zero read as a duration from the
  clock's epoch.
- Filesystem lookup may continue on absence; permission, allocation, and other
  access failures keep their causes instead of selecting another candidate.

## Repository conventions

- Shared command/profile contracts belong to `contracts/`; parsing belongs to
  the subsystem that owns the input language.
- Quirk selection belongs to `quirk/`; execution orchestration, trace, and
  replay belong to `runtime/`; WebGPU ABI contracts belong to `core/abi/`.
- Generated `src/README.md` (from `source-layout.json`) lists current ownership
  directories and compatibility facades; do not duplicate it here.

`source-layout.json` is the architecture manifest, not just a directory
inventory: it owns layers, import permissions, special roles,
compatibility-facade lifecycle, generated sources, and cohesive-module size
justifications. Do not create a second policy file for those facts.

## Architectural decoupling

- Directories are subsystem boundaries, not file buckets. Prefer dependency
  direction: contracts -> helpers -> subsystem implementation ->
  facade/orchestration.
- `src/mod.zig` is the only production Zig file directly under `src/`;
  `source-layout.json` assigns every implementation an ownership directory.
- `core` never imports `full`; extract shared behavior into `core`,
  `backend/common`, or a new contract module instead.
- Backends never import sibling backends; cross-backend sharing belongs in
  `backend/common`.
- `composition/backend_factory.zig` is the ordinary provider construction
  root. Drop-in FFI modules that translate provider-native objects form an
  explicit, source-enforced integration-root set; adding one is an architecture
  change, not a privilege of living in `backend/`.
- Provider configuration is passed at construction; mutable cache paths,
  enablement flags, handles, telemetry, and device identity belong to the
  provider instance, never a process-global shim.
- Non-backend files reach backend-specific behavior through backend-owned seam
  modules such as `backend/dropin_*.zig`, never by importing `backend/metal/*`,
  `backend/vulkan/*`, or `backend/d3d12/*` directly (import-fence enforced).
- `compiler/wgsl` is self-contained except for explicit shared proof/contracts.
- `quirk` holds quirk logic plus shared contracts/proof inputs and never depends
  on backend execution modules.
- Prefer narrow context/state types over monolithic runtime structs across
  subsystem boundaries.
- No new import cycles; extract a smaller contract/state module instead.
- New implementation code must not import `compat/`; the compatibility surface
  is for compatibility tests and declared consumers only.

## Module roots and facades

- Module roots may re-export contracts, normalize public inputs, delegate to
  feature owners, and translate errors.
- Modules used only by their own characterization tests are incubation
  scaffolding (`INV-LIVE-012`), never described as a completed production route.
- Module roots must not own mutable diagnostic state, semantic transforms,
  backend realization, resource state machines, or substantive feature tests.
- Compatibility facades hold only aliases and delegation a declared consumer
  requires, declared in `source-layout.json` with that external consumer,
  reason, owner, consumer-facing test, and removal condition.
- When moving implementation out of a root, facade, or high-fan-in file,
  move definitions first; keep a facade only while an exercised declared
  consumer needs it.

## File size

`architecture.linePolicy` in `source-layout.json` sets the advisory,
justification, and hard-maximum thresholds for handwritten production source
under `src/`. Generated specification or table files need a declared,
reproducible generation contract. Build tooling and tests are outside the
production line gate but still need cohesive responsibilities.

Thresholds are not targets. A file owning multiple state machines, artifact
kinds, input languages, or execution phases splits even below the advisory
signal. Split by cohesive functionality, keep related code together, and never
create a module whose only identity is satisfying a line limit.

File-count reduction and size distribution are campaign observations; report
production, generated, test, and build/tooling code separately, excluding
retained benchmark source snapshots from active-tree totals.

## Semantic inventory decisions

Architecture analysis assigns every production file one decision:

- **Keep**: executable or package root, ABI/FFI boundary, generated
  specification, stable shared contract, or independently meaningful
  algorithm.
- **Merge**: one production importer, no independent contract or test identity,
  same owner as its consumer, and only forwarding declarations, private
  constants, or a tiny private record.
- **Elevate**: a cross-layer semantic fact lives inside one implementation;
  move it to the neutral owner.
- **Recompose**: one file owns multiple state machines, policies, contexts,
  artifact kinds, or pipeline phases; reorganize by those responsibilities.
- **Delete**: unreachable, superseded, duplicated, or an unconsumed
  compatibility facade.

Size, fan-in, fan-out, and co-change frequency inform these decisions; none
decides alone.

## Canonical contract families

Resolve duplicate sources of truth in this order:

1. command kinds, payloads, scope, parser names, trace names, and operation accounting;
2. capabilities and feature identities;
3. error and unsupported classifications;
4. artifact identity and hash fields;
5. timing and dispatch-result semantics;
6. texture-format and binding mappings;
7. backend selection and fallback policy.

Each family has one neutral typed owner; backend-specific conversion and
native control flow stay in the backend. Prefer an explicit tagged union plus a
complete metadata table over type generation.

Command accounting rules are required metadata, not catch-all defaults. Payload
counts must name an existing `u32` field; dynamic capability policies must bind
the command they inspect. Keep independent tests for count normalization and
required capabilities; structural completeness cannot establish their semantic
correctness.

## Formatting

Run these commands from `runtime/zig/`:

```bash
zig build fmt-check
zig build fmt
```

`fmt-check` checks without writing; `fmt` applies Zig's formatter. Both cover the
runtime tree (tests, generated suite roots, build/tooling code), excluding
`vendor/`, `.zig-cache/`, and `zig-out/`. Format generated source through its
generator when a generator check would otherwise reject the result.

`fmt-check` already runs in the default install step, every canonical test
step, and the WGSL CI workflow; artifact-only build steps skip it.

## Imports

- `std` and `builtin` first, then local modules: shared contracts before
  domain-specific imports, grouped by subsystem (e.g. backend modules together).
- Import the narrowest support, contract, value-type, ABI, or feature-local
  state module that owns the symbols. Facades, broad aggregation modules, and
  runtime orchestrators are compatibility surfaces, not default dependencies.

## Naming

- Types, including type-returning factory functions: `PascalCase`.
- Ordinary functions and methods: `camelCase`.
- Local values (including local `const`), parameters, fields, enum tags, and
  module import bindings: `snake_case`.
- Named domain, ABI, and policy constants: `UPPER_SNAKE_CASE`.
- File names: `snake_case.zig`, even when a file centers on one primary type;
  no `PascalCase.zig` files in `runtime/zig/src/`.

Spellings imposed by foreign symbols, generated bindings, serialized schemas,
and external interfaces win over local naming. Rename private symbols with
their consumers when touching their responsibility; never silently rename a
public declaration, error, or serialized field for style. Existing mixed
naming is review debt; no automated naming gate exists.

Name the represented fact: `retained_bytes` distinguishes capacity from a live
allocation count. Avoid `data`, `state`, and `result` when several concepts
share the scope; short names suit unambiguous cases such as a loop index.

## Constants and magic numbers

- Inline `0`, `1`, simple index arithmetic, and language-level sentinels when
  they are the clearest expression of local mechanics.
- Name domain, ABI, policy, threshold, size, retry, and timing values as
  `UPPER_SNAKE_CASE` comptime constants or config values.
- Place module constants after imports; keep type-owned constants with the type.
- Domain-shared constants live in the narrow contract module owning their
  semantics (e.g. `model_texture_value_types.zig`,
  `model_binding_value_types.zig`, `wgpu_core_base_types.zig`,
  `wgpu_texture_base_types.zig`); module-specific constants stay in their
  module. A value used in more than one file has a single source of truth.

## Canonical serialization

- Each schema-owned artifact kind owns its canonical field walk and validation.
- Shared byte, number, string, and key emission belongs in one narrow canonical
  writer module.
- Sharing SHA-256 or JSON emission does not justify a catch-all digest module
  of unrelated artifact walkers.
- Canonicalization refactors must preserve exact bytes, semantic digests, error
  classification, and allocation cleanup through characterization tests.

## Control flow

- Prefer `switch` on enums over long `if` ladders, exhausting meaningful
  alternatives. Use `else` only for a deliberate shared policy; a new enum tag
  must not silently acquire success behavior.
- Use early returns for invalid states.
- Keep fallback behavior explicit and auditable; no silent capability switching.
- Prefer immutable locals in the smallest useful scope, and a tagged union over
  independent flags that permit invalid combinations.
- Extract a helper when it names a responsibility or removes duplicated
  semantics. A forwarding wrapper, generic callback, or new module earns its
  indirection only through an actual contract or consumer.

## Prepared operation parity

These rules govern the command-oriented executor. Ordinary native WebGPU and
package compute programs keep their own submission interfaces but consume the
same applicable shader, resource, ownership, and error contracts.

- Resolve policy, bindings, work shape, entry point, specialization, and
  fallback eligibility before selecting an executor. Immediate, recorded,
  replayed, direct, indirect, and backend-specific paths consume the same
  read-only prepared-operation contract.
- `PreparedOperation` is a borrowed view for one synchronous execution and its
  observer callbacks. Anything that queues, retries asynchronously, records for
  later replay, or otherwise retains it owns an `OwnedPreparedOperation`
  snapshot and releases it explicitly.
- Snapshot cloning rejects pointer forms without a declared ownership rule,
  preserves slice alignment and sentinels, and releases partial allocations on
  failure. Opaque handles and callbacks stay identities whose external owners
  outlive their use; copying an address does not retain the resource.
- `app/prepare.zig` owns canonical command-to-operation conversion; inbound
  adapters may not call the lower-level conversion directly.
- Executor adapters differ only in submission, resource retention, completion,
  readback scheduling, and evidence capture; they never reinterpret bindings,
  invent defaults, change dispatch shape, or select another shader or pipeline.
- Every executor split or merge needs a parity test that changes one plan
  field and proves all applicable adapters observe the change.

Promoted execution interfaces use named types such as `ComputeContext`,
`DispatchRequest`, and `DispatchReport`, under the
[restricted implementation style](#restricted-implementation-style).

## Errors and diagnostics

- Compiler lowering emits a valid typed semantic value or a typed rejection
  with source/node location and reason; placeholder scalar types, shapes, or
  handles are forbidden in promotable semantic artifacts.
- Artifacts carrying semantic rejections are diagnostic-only, barred from code
  generation, parity promotion, and claim-bearing workloads.
- Return explicit error unions (`!T`) for recoverable failures.
- Keep unsupported behavior explicit (`unsupported` taxonomy), never silent no-op.
- Route runtime observability through pipeline/trace/trace-meta contracts; no
  ad-hoc `std.debug.print` in runtime paths, and remove investigation-only
  debug output before committing.
- Suppress intentionally unused interface or callback parameters with
  `_ = param;`, not placeholder naming.

## Comments

- Comments explain why, not what; do not restate the code.
- Use `///` for public intent, ownership, lifetimes, and failure conditions a
  caller cannot infer from the signature, without repeating the identifier.
- Inline comments are for preconditions, control-flow rationale, or non-obvious constraints.
- No inline TODO/FIXME; track follow-ups in the status log (`docs/status.md`, with dated entries in the current `docs/status/*.md` shard).

## Memory

- Allocating functions take an explicit allocator unless the receiving
  struct/context owns one. Structs owning heap-backed state store the allocator
  that releases it and provide an explicit `deinit`.
- Scope temporary allocations with `defer`; use `errdefer` for partial
  initialization and multi-step acquisition rollback. Place each right after
  the acquisition it cleans up unless a tighter scope makes the pairing obvious.
- Keep long-lived caches explicit in owning structs.
- Process-global mutable state is acceptable only when the process is the
  declared resource owner with explicit, tested concurrency semantics; never
  for per-session provider configuration or telemetry.
- Arenas suit bounded lifetimes (one parse, request, or artifact build), never
  hidden long-lived ownership.
- Values crossing a subsystem boundary carry explicit ownership: `borrowed`,
  `scope_owned`, `submit_owned`, `transferred`, or `retained`.
- Ownership transitions belong to the resource owner, not convenience callers,
  each with one success cleanup path and one tested failure cleanup path.
- Submission retains resources until established terminal completion; timeout
  or unknown completion never authorizes reuse or destruction. Terminal failure
  allows retirement but stays an error to consumers.

## FFI and C interop

- Define C function pointer types as `pub const Fn<Name> = *const fn (...) callconv(.c) <ReturnType>`.
- Collect function pointers into a `Procs` struct in `wgpu_types.zig`.
- Required procs are non-optional fields loaded with `loadProc()` (error on missing symbol); optional/conditional procs use a `?` wrapper and `loadOptionalProc()` (null on missing symbol).
- Check optional proc availability before call:
  `if (procs.someFn) |proc| proc(...) else return error.Unsupported`.
- C callbacks use `callconv(.c)` and cast `?*anyopaque` userdata to known state
  structs via `@ptrCast(@alignCast(...))` only under the adapter's alignment,
  nullability, and lifetime contract. Completion status must survive delivery;
  setting a done flag alone does not establish success.
- Keep unavoidable `@cImport` in support or backend-boundary modules, not
  general runtime logic, when a typed seam or ABI module owns that contract.

### Native adapter subset

These rules also govern Doe-owned C/C++ adapters outside this directory, as
routed by `AGENTS.md`. Upstream third-party implementations retain their own
rules. [Architecture](../../docs/architecture.md#implementation-language-and-native-boundary)
owns the language split and native compatibility boundary.

- C++ adapters disable exceptions and RTTI in their owning build. Use plain
  records, explicit result/status values, borrowed spans or views supported by
  the pinned toolchain, and move-only resource owners. No implicit owning raw
  pointers, hidden fallible constructor work, custom operator overloading,
  or new inheritance frameworks. Overrides required by an upstream interface
  stay confined to its adapter.
- RAII expresses an existing cleanup obligation; it never treats cancellation
  as completion or destroys resources still owned by submitted GPU work.
  Acquisition that can fail uses an explicit fallible factory or initialization
  operation, with partial acquisition cleanup.
- Keep templates narrow and local. Public native headers expose C-compatible
  records, opaque handles, status values, and function tables, not templates,
  Zig types, C++ containers, or implementation-specific ownership conventions.
- C shims use typed records/containers and one explicit cleanup path for partial
  initialization. Confine `void *` to declared opaque-handle or callback userdata
  contracts; do not use it to erase ordinary domain types. Generate exhaustive
  dispatch from the owning registry when C cannot enforce completeness directly.
- At every native crossing specify layout, lengths, nullability, alignment,
  calling convention, error delivery, and which side allocates, retains, and
  releases each resource. Preserve existing symbol and callback contracts.

## Determinism and trace

- Identical inputs/config must produce stable decision and trace sequences.
- Preserve hash-chain invariants in trace rows/meta.
- Include enough metadata to reproduce selection and execution outcomes.

## Structural refactor receipts

Before moving runtime or compiler behavior, capture the affected path's
observable contract, covering every applicable field:

- resolved policy and runtime identity;
- semantic or prepared-operation plan;
- operation and submission order;
- shader, pipeline, and specialization identity;
- resource acquisition, transfer, retention, and release events;
- output identity or oracle verdict;
- diagnostic classification and first failure boundary.

After the move, compare the receipt, semantic digest, trace chain, output
oracle, and performance class; remove a facade only after its declared
consumers migrate and parity evidence stays green.

Before a recomposition campaign begins, bind the baseline to one commit and
retain:

- public Zig modules and declarations reachable through `@import("doe")`;
- exported C ABI symbols;
- command parsing and normalized command representations;
- compiler semantic and target-output digests;
- traces, terminal hashes, replay results, and receipt identities;
- backend capabilities and unsupported classifications;
- representative backend outputs;
- clean and incremental compilation measurements, promoted hot-path medians,
  and binary sizes.

Every structural change is exact equivalence, an explicitly approved contract
change, or failure. Public API changes need a manifest diff; no rename, move,
or split may silently change an error name, import path, shader output,
fallback decision, synchronization behavior, or receipt field.

## Testing

- Use inline `test` blocks for private, pure, module-local behavior.
- Use external tests for integration, ABI, backend execution, golden
  artifacts, cross-module characterization, and cross-backend parity.
- Tests that genuinely share setup use one domain-local fixture module.
- Generate suite imports from the owned test inventory, not parallel manual
  aggregator lists.
- Test names are descriptive behavior strings: `test "vendor comparison ignores case"`.
- Use `std.testing.expect` and `std.testing.expectEqual` for assertions.
- Prefer `std.testing.allocator` for allocation-owning code unless the
  allocator choice is itself the behavior under test. Changes to fallible
  acquisition exercise allocation failure at each acquisition point and verify
  partial cleanup and preservation of any previously valid object.
- Lifecycle changes exercise the affected success, failure, cancellation, and
  unknown-completion transitions, including delayed or repeated callbacks where
  applicable. Assert retained ownership and observable results, not source text
  or a second implementation of the state machine.
- Run the narrowest step covering the change: the lane step (`test-wgsl`,
  `test-core`, `test-full`, `test-d3d12`) and `-Dtest-filter=<name>` where it
  fits; use `zig build test` when the change spans lanes.
- When a change alters trace or replay output, run the replay/trace gate for
  that path.
- For WebGPU API-surface changes, update config coverage and benchmark
  contracts in the same change.

## Consistency checks and review

| Concern | Enforced mechanically | Still requires review or behavioral evidence |
| --- | --- | --- |
| Formatting | `zig build fmt-check` | Clear names and useful documentation |
| Ownership boundaries | `zig build import-fence source-layout` | Correct semantic owner and a necessary abstraction |
| Production file size | `zig build line-limits` | Cohesion, even below the configured thresholds |
| Generated contracts and suites | `zig build webgpu-abi test-inventory` | Independent oracle and meaningful failure coverage |
| Types and command coverage | Compilation of the affected consumers | Correct validation, execution, and complete exercised paths |
| Generic machinery | Type checking of instantiated interfaces | Small, inspectable `comptime`; explicit dependencies and no hidden policy |
| Native adapters | Owning build flags and exercised ABI fixtures | Restricted C/C++ subset, caller ownership, and upstream compatibility |
| Resource lifetime and allocation | Focused lifetime and allocation-failure tests | Complete ownership reasoning across asynchronous and foreign calls |

These are enforcement surfaces, not a claim of complete repository coverage.
Do not replace compiler checks with source-spelling tests. A new gate must reject
an invalid fixture and accept a valid one through the real checker; behavior and
ownership still require independent failure-path tests. Missing enforcement is
review debt, never evidence that an invariant holds.

The default install step and the canonical test steps already run the format,
layout, ABI, line-limit, and inventory checks; invoke one by name only when
changing what it checks.

Systematic reviews follow the [hierarchical review ledger](reviews/README.md):
its levels are distinct, a partial cleanup does not review the whole file, and
its generated queue decides whether earlier reviews remain current.

For each cleanup, work through one named responsibility:

1. Read its charter, consumers, and existing tests. Use the generated
   `reports/architecture/` inventory for duplicate declarations, dependency
   edges, and reachability; inspect the code before accepting a suggested
   merge or deletion.
2. Name the ambiguity or duplication being removed and what must stay
   identical (public names, errors, serialized bytes, resource lifetime).
3. Make the smallest coherent change, migrate consumers, and remove the
   obsolete path, preferring an existing typed contract or standard-library
   operation over another registry, wrapper, or generic utility.
4. Format, run the structural gates and behavioral tests that cover the touched
   responsibility, and retain an equivalence receipt when behavior moved.
   Style-only edits need no tests that merely repeat the implementation; new
   enforcement must show it rejects the condition it claims to detect.
5. Record remaining debt with its semantic owner and next concrete target in
   the live status shard. When a finding generalizes, update this guide and the
   owning compiler check, gate, or behavioral test in the same change; identify
   review-only obligations explicitly. Policy values and module inventories
   stay with their owners. Report cleanup deltas under
   [refactor law](../../docs/process.md#language-and-cleanup-evaluation).

Performance claims need the frozen application procedure. A cleaner diff, fewer
allocations, or a successful compile is not performance evidence.
