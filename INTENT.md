# INTENT: Doe

Parent: none

This file owns enforceable system invariants. [GOALS.md](GOALS.md) owns desired
outcomes and exclusions; [CATSCAN.md](CATSCAN.md) assigns component authority.
Concrete implementation and qualification contracts refine these requirements.

## Program identity and independent correctness

- Identity-bearing artifacts cryptographically bind source WGSL, pipeline
  layout, lowering policy, command graphs, and outputs. Stage inputs and outputs
  are versioned and hash-linked where required; qualification evidence remains
  mandatory even when ordinary applications disable optional tracing.
- Promotion requires a frozen independent semantic oracle and declared exactness
  class. Physical execution traces and reproducible output receipts govern
  correctness and performance claims; compiler syntax checks do not prove
  hardware suitability.
- Hardware evidence binds the physical adapter and driver. Package, native,
  browser, simulator, and hardware results cannot inherit one another's claims.
  Simulator execution cannot qualify physical hardware.

## Execution and lifetime safety

- Fawn-controlled automated browser mutations use the trusted workspace
  dispatcher. Durable owner generations serialize handoff, admission and
  dispatch; stale commands are rejected, and restart restores authority before
  accepting commands.
- Versioned observations bind authorized intent, targets and dispatch-time
  preconditions. Browser/external effects are not promised atomic.
- Persist browser-action intent before dispatch. Preserve completed, failed,
  unknown and separately provable never-dispatched outcomes. Missing completion
  never permits implicit resubmission; recovery cannot erase uncertainty.
  [The workspace contract](docs/fawn-workspace-contract.md) defines reconciliation,
  in-flight handoff limits and prospective acceptance. These requirements do not
  claim an implemented service or inherit GPU qualification.

- Unsupported capabilities, overflow, provider/backend mismatches, and faults
  fail closed with inspectable, actionable errors. No hidden heuristics,
  undocumented fallback modes, silent no-ops, or unverified CPU substitution.
- The compiler owns shader meaning and target lowering; the runtime owns
  resources, physical execution, synchronization, and lifetime. Host adapters
  never substitute shader behavior from source patterns.
- Immutable descriptions, device-specific prepared resources, and invocation
  state have separate owners. Exact bytes, entrypoint, layout, effective compiler
  options, device identity, and native allocation generations govern reuse;
  hashes only locate candidates. Affected changes invalidate prepared state.
- Compatible parameter changes may reuse resources. Structural replacements
  prepare before activation; failed preparation preserves the working program.
  Incompatible resident-state changes require explicit approval.
- Submission carries resource obligations until established terminal completion.
  Timeout or cancellation stops waiting, not GPU use. Unknown completion forbids
  unsafe reuse or destruction; rollback never abandons live GPU ownership.
  Terminal failure permits cleanup but never successful output. Device and
  concurrent-work isolation remain required.
- Ordinary WebGPU, command execution, and prepared programs share semantic rules
  without requiring a universal interpreter. Ordinary execution uses a selected
  version and remains usable without development tools, detailed tracing, or
  artifact publication.

The [lifecycle contract](docs/native-device-lifecycle-contract.md),
[callback contract](docs/native-callback-contract.md), and
[reusable-program contract](docs/reusable-compute-programs.md) own concrete
transition, invalidation, and acceptance details.

## Implementation and policy integrity

- Controls, deterministic defaults, tunables, and thresholds come from versioned
  config/schema. Production policy changes cannot rely on undocumented manual
  toggles. Runtime-visible field changes include schemas and migration notes;
  process/gate documentation and status tracking change with the contract.
- Zig compile-time checks establish structural completeness, not behavioral or
  lifetime correctness. Explicit allocators, checked arithmetic, initialization,
  acquisition rollback, bounded caches, and backend-private state require
  independent behavioral and lifetime tests.
- Runtime behavior placeholders and synthetic backend timing/capability state
  are forbidden. Implement native behavior or explicit unsupported taxonomy.
  Any native/runtime import or file matching `*_runtime_state.zig` is rejected by
  `runtime/zig/tools/check_core_import_fence.py`.
- Only benchmark/gate bootstrap thresholds may be placeholders, explicitly
  flagged in config and tracked in gate policy and the relevant live status shard.
- Production latency-critical behavior remains deterministic Zig first. The explicitly
  selected isolated C migration must preserve the same invariants and cannot
  replace production before its declared qualification gates pass. A runtime condition may
  be removed only when current proof/config discharges its actual preconditions;
  otherwise retain and measure the guard. Elimination never moves hot-path
  execution into a runtime Lean interpreter. Verification modes and source-bound
  proof admission follow [process law](docs/process.md#verification-precedence).

## Promotion and evidence integrity

- DoeLab tests proposed improvements offline. Frozen tests and independent
  comparisons govern reversible promotion; application requests cannot silently
  rewrite the selected compiler or runtime. Retain failures and rejected trials.
- DoeProof records evidence impartially for every provider. Its receipts and
  supporting-feature value neither prove runtime adoption nor become
  prerequisites for ordinary execution.
  Compiler and runtime contributions require separately attributed evidence.
- Runtime ownership requires the [governed comparison](docs/runtime-ownership-decision.md).
  Work equivalence, timing completeness, controls, audits, and claim eligibility
  follow the [performance contract](docs/performance-strategy.md). Prose cannot
  promote artifacts, bypass failing gates, or reinterpret historical verdicts.
- Follow [stage order](docs/process.md#stage-order) and
  [blocking gate policy](docs/process.md#gate-policy-and-failure-precedence).
  A later-stage result never excuses an earlier failure; unmet `lean_required`
  verification blocks release. Advisory performance cannot override that failure.

## Evidence

- [CATSCAN gate](bench/gates/catscan_gate.py) checks charter structure and routing;
  it does not replace semantic, behavioral, or physical qualification.
- Execution contract tests, independent compiler parity, lifecycle fixtures, and
  the declared DoeProof schema establish their respective obligations.
- [Process law](docs/process.md) and component charters select the applicable
  commands and source-bound artifacts. Missing evidence remains missing.
