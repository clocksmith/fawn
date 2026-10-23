# Doe C migration: retained baseline and acceptance gates

## Scope and present state

This is the initial inventory for the explicitly requested, incremental C
migration on the local experimental branch. It is not a mechanical translation,
production switchover, new architecture authority, or claim that a rewrite is
complete. Preserve retained Zig, the original main checkout, and previous
language-experiment receipts. Changes are committed only on the requested local
experimental branch; no public push, system installation or external-machine
operation is part of this campaign.

The immutable baseline is commit
`665475907494d2095a760596218b4c2d363cb436`. The
[machine-readable manifest](zig-inventory.json) includes every tracked `.zig`
blob at that revision, even when sparse checkout does not materialize the file:

- 809 files, 9,810,557 bytes, 222,409 lines.
- 628 handwritten implementation files: 155,898 lines.
- 137 handwritten test/suite/support files: 54,605 lines.
- 9 generated files: 1,804 lines, comprising 7 test suite roots and 2 production
  definitions. Including generated definitions gives 630 production-use paths.
- 1 build root: 1,746 lines.
- 34 other files: 8,356 lines, comprising 24 retained historical source/probe
  snapshots and 10 repository-only benchmark/tool sources.

**Zero production Zig paths have an accepted C replacement in this baseline.**
Experimental C code created beside it does not change that acceptance count.
All migration stages below remain unqualified. There is no GPU execution or
physical qualification from this inventory, including no Vulkan, Metal, or D3D12
physical qualification. Host compilation, sanitizer passes, fixture output,
bytecode validation, and simulated completion never inherit that qualification.

## Accepted experimental coverage

The [first compiler increment](../../runtime/c/compiler/MILESTONE_1_RESULTS.md)
is locally accepted at implementation commit `25c41ee`, with the independent
test hash-path repair at `37d98fe`. It admits the unchanged residual-add source
and the retained u32 seed through a source-built C API and CLI. Its independent
suite checks emitted instruction behavior, target validation, native C/C++
consumption and failure boundaries. This is not adoption by the existing MSL
benchmark or a source-to-GPU application, and does not change the zero accepted
production-owner count.

The [second frozen increment](../../runtime/c/compiler/MILESTONE_2.md) adds
structured control flow and checked per-resource index behavior for the real
neighbor-sum fixture. Its [accepted results](../../runtime/c/compiler/MILESTONE_2_RESULTS.md) record
independent instruction/native checks and the increased static stack footprint. These declared coverage increments do not replace the complete
compiler and consumer requirements below.

The [third increment](../../runtime/c/compiler/MILESTONE_3_RESULTS.md) admits
the unchanged existing SPIR-V emitter-consumer protocol. Its actual corpus
coverage remains narrow: two paths containing one unique source, with 96 paths
explicitly rejected. The [source-bound corpus ledger](corpus-coverage-m3.json)
separates every reached stage and unrun backend qualification. This is component
consumption, not runtime adoption.

The [fourth increment](../../runtime/c/compiler/MILESTONE_4_RESULTS.md) adds
typed scalar expressions and immutable locals, with exact bounded decimal
materialization. The [updated ledger](corpus-coverage-m4.json) records four
admitted paths /three unique sources; it does not replace the prior ledger or
qualify the remaining 94 paths. Workspace growth is measured and bounded, not a
permanent allocation policy for the complete compiler.

The [fifth increment](../../runtime/c/compiler/MILESTONE_5_RESULTS.md) adds generic
flat uniform layout/member access and a separately versioned caller-workspace API
through the same compiler pipeline. ABI1 and legacy reflection remain compatible.
Its independent native/semantic/fault tests pass; corpus coverage is five paths /
four unique sources, with 93 explicit rejections. The old API frame decreases to
96,272 bytes; v2 caller workspace is 96,208 bytes, not eliminated memory. This
remains standalone compiler coverage, not runtime adoption or production replacement.

The [sixth increment](../../runtime/c/compiler/MILESTONE_6_RESULTS.md) unifies
scalar expression parsing/lowering across indices and comparisons and admits the
five compute input builtins, three-dimensional local sizes and no-op entries.
Its [ledger](corpus-coverage-m6.json) records 11 paths / nine unique sources
through the unchanged consumer, target validation and instruction execution;
87 paths still reject. Workspace decreases to 77,760 bytes and ABI1 frame to
77,824, without changing public headers. The
[remaining-owner roadmap](COMPILER_ROADMAP.md) prioritizes scalable job storage,
function state/control flow and the complete compiler dependency chain. It does
not declare an accepted production replacement.

The [seventh increment](../../runtime/c/compiler/MILESTONE_7_RESULTS.md) adds
configurable caller-owned job storage, mutable scalar state, lexical scopes,
structured loops and short circuit through the same compiler. All five real
loop witnesses pass independent emitted-instruction execution. The unchanged
consumer now admits 17 paths / 15 unique sources, with 81 explicit rejections in
the [new ledger](corpus-coverage-m7.json). Fixed workspace/frame decrease to
73,400/73,488 bytes, while the broader CLI job explicitly owns 1,995,208 bytes.
Neither storage relocation nor expanded corpus coverage qualifies complete
WGSL, other targets, production callers or hardware. Generic module-semicolon
admission applies across APIs; experimental old exclusions are not permanent
language-dialect requirements.

The first [CPU runtime owner](../../runtime/c/futures/RESULTS.md) is independently
accepted as an isolated C library for future IDs and completion queues. It retains
issuance-boundary selection, callback reentry, actual condition waits and explicit
retirement, with tested cleanup failures and native C/C++ consumption. Payload
allocation, instance/device leases, production runtime consumers and GPU completion
remain unqualified. This does not change the zero production-replacement count.

## Existing authority, not a parallel policy

Owner paths and layer labels come from
[the existing source-layout policy](../../runtime/zig/source-layout.json), with
nearest inherited charters for files outside its production root. The manifest
contains references and observations, not copied dependency rules. Consult the
[generated source map](../../runtime/zig/src/README.md),
[compiler architecture](../shader-compiler-architecture.md),
[runtime architecture](../architecture.md),
[style and native boundary rules](../../runtime/zig/STYLE.md), and
[charter protocol](../component-charters.md) when an implementation changes.
A changed boundary must update its actual owner and checks together; this plan
cannot silently authorize a different architecture or excuse a failure.

The existing separation remains decisive: compiler semantics and target
emission; native WebGPU API validation, references and object lifetimes;
backend-private physical execution and completion; host adapters and public
consumers. Shared mechanics may be redesigned in C, but policy must stay with
its owner. Migrate a coherent responsibility and its consumers, not each Zig
file one-for-one. Do not create a second source of truth, source-pattern shader
emulation, hidden fallback, or generic framework merely to reduce line counts.

## Staged acceptance gates

Each stage admits an independently useful increment before expanding coverage.
Passing a bounded increment is not completion of that stage. Record the exact
source revisions, build inputs, tested surface, independent oracle, rejected
inputs, failure cases, and remaining exclusions with each candidate.

1. **Inventory and characterize.** Require an exact one-to-one listing of all
   tracked baseline Zig blobs, matching hashes/size/line counts, explicit roles,
   owner references, generated provenance and test catalog routes. Freeze
   representative positive and negative cases before moving behavior. Preserve
   baseline outputs and first failure boundaries. This inventory is the starting
   point, not the behavioral characterization of 809 files.
2. **Independently usable compiler first.** Expose a narrow documented C API and
   executable with explicit inputs, owned outputs, source-linked diagnostics and
   rejection. Exercise source through lexer/parser, semantic validation, typed
   IR, transformations and target emission without runtime adoption. Start with
   a declared useful subset, including array load/compute/store, and reject
   unsupported syntax/types/targets rather than silently accepting or dropping
   them. Compiler completion requires disposition of all existing WGSL and TSIR
   owners, declared targets and production callers; equivalent valid/invalid
   corpus behavior; differential retained-Zig checks plus independent semantic
   oracles; target validation; robustness, entrypoint selection, overrides,
   control flow, source location and allocation-failure regressions. Explicitly
   account for MSL, SPIR-V, HLSL, native/toolchain DXIL, WGSL rewrite, browser
   WASM, TSIR and CSL paths under their existing declared support boundaries.
   Preserve `TranslationInfo` workgroup sizes, size-buffer requirement,
   buffer/texture dispatch preconditions and entrypoint binding metadata,
   including clone/free and partial-failure behavior. Preserve compiler/config
   identity, reflection publication, diagnostic lifetime and cache-key semantics;
   stale or differently configured artifacts must not be silently reused.
   Enumerate public `@import("doe")` compiler exports and actual consumers;
   retain an exercised compatibility binding or explicitly migrate each caller
   before removing that surface. Counts alone cannot close this gate.
   Byte-identical artifacts are required only where the existing identity/format
   contract requires them. A bounded no-heap seed is not a global constraint:
   full compiler/runtime jobs use explicit allocator/storage owners and tested
   cleanup, including allocation-failure injection where allocation is needed.
   There is no universal zero-allocation promise or mandatory arena framework.
3. **Contracts and runtime integration.** Preserve public C ABI symbols, layouts,
   calling conventions, lengths, nullability, error delivery, generated header
   provenance and versioned schemas. Introduce checked size arithmetic, explicit
   allocator/ownership contracts, exhaustive status/command dispositions and
   failure cleanup before wiring C owners into real command paths. Prove the
   actual consumer route without silently falling through to retained Zig.
   Generated definitions must be regenerated from the pinned authoritative inputs,
   not manually translated tables. Existing runtime, command and prepared-operation
   semantics must remain independently testable.
4. **Objects, lifetimes and backends.** Migrate native objects and each backend
   as bounded ownership units. Cover allocation failure at every acquisition,
   immediate caller release, retained command/resource identity, repeated and
   concurrent work, callbacks/futures, cancellation, timeout, device loss,
   rejected submissions, rollback and terminal cleanup. Unknown completion must
   retain obligations and prohibit unsafe reuse/destruction. Preserve first
   errors and failed replacement behavior. Qualify Vulkan, Metal and D3D12
   separately on the required physical host/adapter/driver with unchanged
   workloads and independent readback oracles. Missing platform access leaves
   that backend blocked, not migrated or qualified. Keep backend siblings
   separate; sharing does not transfer qualification.
5. **Consumers and standalone adoption.** Exercise unchanged native C consumers,
   the proc-table bridge, JavaScript/package paths, browser/compiler paths and
   other declared runtime/tool callers against the intended C artifacts. Check
   ABI, install/load, entry selection, error/callback delivery, early releases,
   shutdown and rollback. Preserve provider attribution; a browser's own WebGPU
   execution is not Doe runtime evidence. Required package/application and
   physical tests remain distinct from compiler/unit tests.
6. **Complete active migration and assess benefit.** Account for every baseline
   active owner and every subsequently added Zig source. Each has a source-bound
   accepted C replacement, a regenerated equivalent, or an explicitly approved
   removal with traced consumers and preserved tests. All declared production
   builds and consumers must work without a Zig implementation dependency.
   Migrate build/tool/test infrastructure necessary to keep that path reproducible.
   Historical receipts and the retained reference are not translation targets
   and must not be deleted to make the count reach zero. Only then consider a
   production-default change, with the corresponding canonical authority update,
   reversible rollout, full relevant gates, and independently qualified backends.

For every accepted increment, apply
[refactor and language-evaluation law](../process.md#language-and-cleanup-evaluation):
report preserved outputs/first errors, defects caught, ownership changes, actual
consumers, clean/incremental/focused-check cost, artifact size, and measured
friction. Compiler, host preparation and completed physical execution costs stay
separate. Unmeasured debugging, performance, maintenance and regression-escape
benefits remain unclaimed; a lower line count or preferred language proves none.

## Verification routes and representative source

The manifest references each cataloged suite membership, including platform
restrictions and test support entries in
[the test inventory](../../config/zig-test-inventory.json). Its lexical inline
`test` counts and literal imports are inspection aids, not AST coverage, executed
counts, a complete consumer graph or permission to delete code. Generated suite
membership does not override platform conditions or nested suite includes.
Use the [canonical runtime test entrypoints](../../runtime/zig/README.md#build-and-test):
`test-wgsl`, `test-core`, `test-full` and the affected backend target. Focused
filters must execute matching tests. Native lifecycle/callback/readback fixtures,
[device lifecycle](../native-device-lifecycle-contract.md),
[callback](../native-callback-contract.md), and
[reusable-program](../reusable-compute-programs.md) contracts remain necessary.
Existing C fixtures include `runtime/zig/tests/native_recorded_compute.c`,
`native_async_pipeline.c`, and the `d3d12_*_readback.c` family; a host-only pass
cannot satisfy their physical acceptance.

Three baseline source-bound fixtures extend the initial scalar increment:

- [Residual add](../../bench/inference-pipeline/kernels/residual.wgsl): three
  storage `array<f32>` bindings, `@workgroup_size(256)`, and
  `out[idx] = a[idx] + b[idx];`. This is a compact array load/compute/store case.
- [Neighbor sum](../../bench/fixtures/wgsl-corpus/webgpu/sample-prefix-sum.wgsl):
  `array<u32>` input/output, an index-zero branch with early return, otherwise
  `input_values[index - 1u] + input_values[index]`. Despite its filename this
  fixture computes adjacent sums, not a complete prefix scan.
- [Particle integration](../../bench/fixtures/wgsl-corpus/game/particle-integrate.wgsl):
  load a `Particle` struct, update vector position using velocity and override
  `dt`, then store the struct. This adds aggregate layout, vectors and overrides.

These fixtures have no complete dispatch/buffer admission envelope by themselves;
freeze safe bindings, dispatch bounds and independent expected outputs in their
harness. Do not infer full application or robustness coverage from one fixture.

## Reproduction and provenance

From the repository root:

```sh
python3 docs/c-migration/inventory.py --check
python3 docs/c-migration/inventory.py --source-revision 665475907494d2095a760596218b4c2d363cb436
```

The second command regenerates only `zig-inventory.json`. It reads exact Git
blobs, not potentially modified working-tree bytes. The committed-source
inventory intentionally does not absorb concurrently developed experimental C
code or uncommitted canonical-document edits. Refreshing the source revision
requires consciously reviewing the resulting scope change.

The baseline [top-level LICENSE](../../LICENSE) is present and explicitly MIT,
Copyright (c) 2026 Doe contributors. It is not absent or inferred from prose.
Retain notices and source identities during migration and distribution; do not
relicense generated or borrowed interfaces by changing their implementation
language. [Licensing](../licensing.md) owns the separate WebGPU header,
Khronos SPIR-V definitions, Dawn benchmark/bridge, ONNX and other third-party
boundaries. Preserve upstream notices and generator input identities; do not
copy oracle implementation code into runtime owners or treat an ABI header as
permission to reuse unrelated implementation source. Any unclear new source's
license remains unresolved until verified, not presumed MIT.
