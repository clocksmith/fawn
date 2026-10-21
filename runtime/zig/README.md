# Doe Zig runtime

`runtime/zig/` owns the native runtime, WebGPU ABI, WGSL compiler, backend
implementations, TSIR lowering, and native tools. Public JavaScript behavior is
exposed through `packages/doe-gpu/`; benchmark and claim orchestration remains
under `bench/`.

## Source ownership

Start with the [generated source map](src/README.md). Its responsibility labels,
build views, and import boundaries come from [source-layout.json](source-layout.json).
The map distinguishes WebGPU object ownership, command execution through the
WebGPU ABI, shared execution services, and module incubation. Historical names
such as `core` and `full` are not evidence of which build ships a module.

Route changes by responsibility:

| Owner | Responsibility |
| --- | --- |
| `src/compiler/` | Shader semantics and target lowering |
| `src/native/` | WebGPU objects, resource lifetimes, and native provider behavior |
| `src/backend/` | Backend interfaces and platform execution |
| `src/dropin/` | Exported WebGPU ABI routing and behavior policy |
| `src/core/`, `src/full/` | Commands executed through the WebGPU ABI |
| [`packages/doe-gpu/`](../../packages/doe-gpu/README.md) | Public JavaScript integration |
| [`runtime/bridge/`](../bridge/) | External application adapters |

Command execution through the ABI is distinct from implementing native WebGPU
objects behind it. Use the generated map for individual module placement and
allowed dependencies; this table does not redefine that policy.

Use the nearest owner rather than adding catch-all utilities. The import fence
enforces dependency direction; organization quality still requires reviewing
responsibility splits and actual consumers.

The [review ledger](reviews/README.md) tracks examination from individual files
through directory relationships and complete runtime paths. Its generated queue
records coverage separately from architecture decisions and passing tests.

## Build and test

Run from `runtime/zig/`. Start with the test that exercises the changed owner,
then broaden only for a named unanswered question.

| Decision | First local check | Broader evidence, when required |
| --- | --- | --- |
| WGSL semantics, diagnostics or emission | `zig build test-wgsl -Dtest-filter=<behavior>` | Full `test-wgsl`; independent target and physical execution evidence |
| Command, contract or shared execution behavior | `zig build test-core -Dtest-filter=<behavior>` | Full `test-core` and affected executor parity |
| Native object or full runtime behavior | `zig build test-full -Dtest-filter=<behavior>` | Relevant C/native fixture; matching physical backend qualification |
| Import boundary, source placement or size policy | `zig build import-fence source-layout line-limits` | Review of actual consumers and ownership |
| Formatting only | `zig build fmt-check` | No behavioral qualification from formatting |

The local check establishes only its exercised behavior. A host test cannot
qualify GPU execution, and `test-full` is a suite name, not a release verdict.
Choose a test name that actually exists in the owning module or test inventory.
`zig build test` runs the broad host suite; benchmark and release commands stay
in their owning runbooks and `zig build --help`.

Use the configured Zig toolchain and the [style guide](STYLE.md). `zig build fmt`
formats owned Zig files; `fmt-check` is also a dependency of the default install
and canonical test suites. Both exclude vendored sources and build outputs.

`-Dtest-filter=<substring>` selects test names in `test`, `test-core`,
`test-full`, `test-d3d12`, `test-wgsl`, `test-bench-shader`, and
`test-bench-compilation`, `test-bench-host-hotpaths`, `test-bench-metal-compute`,
`test-bench-metal-staged-write`, and `test-runtime-compile-report`. Check the executed test count:
a filter matching no names runs no tests, and a filtered run does not qualify
the complete suite.

Backend-specific builds and tools are listed by `zig build --help`. Hardware
execution requires the matching host, SDK, driver, and declared backend lane.
The [compiler benchmark contract](bench/compiler/README.md) describes the
diagnostic stage tool's sampling, error handling, and timing boundaries.
The [host benchmark contract](bench/host_hotpath/README.md) describes local
variant observations, ownership, and diagnostic output migration.
The [Metal benchmark contract](bench/entrypoints/README.md) separates CPU
contract tests from physical Metal execution.

## Runtime contract

- Unsupported behavior is typed and explicit.
- Core must not import full-surface modules.
- Runtime-visible selection comes from config or declared call inputs.
- Promoted execution must preserve provider, backend, adapter, driver, command,
  output, and timing identity.
- Cache, timestamp, trace, and receipt failures cannot silently change semantic
  success.
- Synthetic backend state and hidden fallback are forbidden.

## Metal adapter identity

The native adapter owns its selected retained Metal device. Public adapter info
uses that device's name and unified-memory classification; low-power remains a
separate selection trait. Unavailable PCI-style vendor and device IDs stay zero.
They must not be fabricated or used as the sole physical-admission test on Metal.

The existing `doeNativeAdapterGetInfoStruct` export validates the native adapter
and copies metadata from its selected device, without exporting a Metal handle.
Its registry ID identifies the current IORegistry device, not a persistent ID
across reboots. Physical qualification must bind it to independent Metal
enumeration and preserve the workload's output and lifecycle checks. Metadata
projection tests alone do not qualify GPU execution.

## Build tiers

`compute`, `headless`, and `full` tiers expose increasing WebGPU surface area.
Tier selection is a build contract, not evidence that every operation is
supported. Consult [`../../docs/doe-support-matrix.md`](../../docs/doe-support-matrix.md)
and current artifacts before making compatibility claims.

## Compiler and spatial paths

- WGSL architecture:
  [`../../docs/shader-compiler-architecture.md`](../../docs/shader-compiler-architecture.md)
- TSIR contract: [`../../docs/tsir-contract.md`](../../docs/tsir-contract.md)
- CSL architecture: [`../../docs/csl-architecture.md`](../../docs/csl-architecture.md)
- Proof elimination:
  [`../../docs/lean-bounds-elimination-design.md`](../../docs/lean-bounds-elimination-design.md)

Detailed tool invocations belong in focused runbooks and `zig build --help`,
not this module entrypoint.

## Native command ownership

Canonical Zig tests inject allocation failures into private fused-command
construction and check cleanup and publication. The direct C fixture at
[`tests/native_recorded_compute.c`](tests/native_recorded_compute.c) exercises
the exported constructors on AMD Vulkan after caller references are released.
Build, execution, and retained-library reproduction commands are in the
[allocation checkpoint](../../bench/out/compute-program/20260906-recorded-allocation/README.md).
Package qualification separately verifies ordinary command ownership across
the JavaScript/native boundary.

[`tests/native_async_pipeline.c`](tests/native_async_pipeline.c) exercises the
public async render-pipeline callback, immediate caller release, and eventual
device cleanup on Linux Vulkan. Link it against the retained package library
with the same C build recipe. The Zig suite also injects snapshot allocation and
worker-start failures, compares exact request identities, and verifies callback
leases for compute and rendering.

[`tests/native_occlusion_query.c`](tests/native_occlusion_query.c) checks ordinary
public recording, submission, query resolve and pixel readback on Linux Vulkan.
The [query checkpoint](../../reports/maintenance/20260928-production-query/README.md)
retains the isolated build recipe and native command-order observation.

## Build edit measurements

Run `python3 runtime/zig/tools/capture_build_measurements.py` from the repository
root. The tool measures a clean build, a no-change rebuild, and each exact source
edit in `config/zig-build-measurements.json`, using a private source snapshot and
cache. Every edit starts from a restored baseline. The report separates elapsed
time, per-build process RSS, and artifact size; it does not modify active work.
See the [architecture audit](../../docs/status/runtime-architecture-audit.md)
for the current recipe disposition and scoped evidence.

## Windows D3D12 recorded compute subset

The public recorded compute path admits an explicit or automatic pipeline
layout with one group and one to eight densely numbered storage buffers starting
at group 0 / binding 0. Each binding is a read-only or read/write fixed-size or
runtime-sized array of `u32`, `i32`, or `f32`; layout access must match the shader. The WGSL entry
point is `main` with a one-dimensional workgroup. Dispatch is one-dimensional.
Fixed-size arrays require dispatch to fit the declared extent. Runtime-sized
arrays admit padded dispatch through compiler-emitted bound-view write guards;
any compiler bounds-elimination obligations still validate before submission.
Static binding offsets must meet the advertised storage-buffer alignment, and
views must fit the allocation. Fixed arrays require the declared size; runtime
arrays require at least one scalar element. `WGPU_WHOLE_SIZE` selects the bytes
remaining after the offset. `arrayLength` returns the descriptor's
element count, including when the binding is shorter than the allocation.
DXC must be available on the invoking process's PATH.

Compiler analysis and HLSL emission are reused. Proof obligations remain attached
to the pipeline and are validated at recording and submission. The queue permits
copies and buffer clears around admitted compute, retaining buffers, pipeline, root signature and
per-dispatch descriptor heaps through completion. Indirect dispatch, other
binding shapes (including sparse slots, other groups and aliases), overrides,
and mixed render/compute/copy
submissions remain rejected. Pure render submission is a separate path.

`tests/d3d12_compute_readback.c` dynamically loads the DLL and checks a CPU
numerical oracle, early caller release, repeated dispatch ordering, and rejection
without modifying a prefilled readback buffer. Build it with the pinned Zig C
compiler and `-I vendor/webgpu-headers`, then pass the DLL path and optionally
`float`, `int`, `repeat`, `reject`, or `unsupported`. The last mode checks pipeline
rejection for an aggregate array. `tests/d3d12_buffer_readback.c` retains copy-only
coverage. These focused tests do not qualify full WebGPU/D3D12 support or speed.

`tests/d3d12_runtime_array_readback.c` adds bound-length numerical oracles. Its
`short` mode checks a 16-byte view over a 32-byte allocation, preserving the tail;
`rebind` checks two descriptor lengths in one command list. `fixture` runs the
unchanged increment shader from `native_recorded_compute.c` using this harness's
explicit layout. The full original application remains unqualified. The scalar,
repeat and rejection modes are also available.
`recover` rejects an aggregate-array pipeline, then creates and executes a valid
runtime-array pipeline on the same device.

`tests/d3d12_auto_layout_readback.c` exercises the same scalar-storage subset
with no explicit pipeline layout. It retrieves and releases the pipeline-owned
bind group layout, including after a sibling pipeline is released. `mismatch`
checks rejection of a bind group made from that sibling automatic layout while
preserving a prefilled output; `fixed` covers a fixed-size scalar array. The
runtime-array modes above remain available. These checks cover automatic layout
ownership and binding identity within the admitted subset. The original
`native_recorded_compute.c` still requests Vulkan and includes texture-copy
operations outside the D3D12 subset; its unchanged shader is not a whole-application
qualification.

The same automatic-layout fixture provides `multi`, `multi-explicit`,
`multi-eight`, `multi-write`, `multi-short` and `multi-rebind` modes. Distinct
input buffers, reversed declarations/layout entries and independent CPU arithmetic
check SRV/UAV register mapping, per-binding `arrayLength`, shortened views,
descriptor replacement and ordering across repeated writes. Caller references
are released before completion. `multi-alias` and `multi-offset` reject an
invalid batch without changing the prefilled readback; `multi-sparse` retains
the sparse public-layout rejection. `multi-recover` rejects a wrong-access
explicit layout, then executes a valid automatic layout on the same device.
`multi-dynamic-layout` rejects unsupported dynamic-offset layout declarations and
then executes a valid automatic layout. `multi-dynamic-offsets` checks that a
nonzero dynamic-offset count invalidates recording with a validation error and
preserves the prefilled readback. D3D12 dynamic offsets remain unsupported; they
must not be silently discarded.
`multi-view`, `multi-view-whole`, `multi-view-short` and `multi-view-rebind` check
aligned SRV/UAV subranges, whole remaining size, per-view lengths and static
rebinding with the same scalar shader. Exact full-allocation readback checks
both computed words and untouched prefix/suffix data. `multi-view-range` rejects
out-of-bounds and overflowing ranges, then executes the valid view.
`overdispatch`, `guarded-tail` and `wide-tail` check runtime-array tail writes
against independent output, including the retained Vulkan and package shader
sources. `multi-view-tail` checks that padded dispatch preserves the allocation
outside its bound view. The explicit runtime-array fixture also runs
`overdispatch`. Runtime-array `reject` modes retain sentinel/recovery checks with
65,536 workgroups, beyond the admitted 65,535 limit; fixed-array rejection stays
unchanged. These native tests do not qualify the package or complete simulation.
These are public-ABI correctness checks, not additions to the matched benchmark
population. The generated backend workload catalog and comparison gates remain
unchanged; CLI qualification does not transfer to this public path.

## Native command-buffer consumption

The native C submission entry point consumes each command buffer once, including
every supplied buffer when the batch is rejected. Duplicate or previously
submitted buffers fail validation before any commands execute. Retained resource
ownership is unchanged; a fresh command buffer can submit after rejection.
Reusable compute programs remain a separate explicit API.

`tests/d3d12_command_consumption.c` checks duplicate and repeated submission on
physical D3D12 with exact unchanged readback and fresh-command recovery. Build it
with the pinned Zig C compiler and `-I vendor/webgpu-headers`, then pass the full
DLL path and `duplicate` or `reuse`. Shared admission unit tests additionally
check failed batches, foreign-device buffers and mapped retained resources.
These tests do not establish physical Vulkan/Metal lifecycle or concurrency
qualification. The [WebGPU consumption rule](https://github.com/gpuweb/gpuweb/pull/4608)
also applies to rejected submissions.

## Windows buffer-clear parity boundary

Recorded `clearBuffer` zeroes aligned ranges on D3D12 default and readback
buffers with `CopyDst` usage. It preserves command order with copies and admitted
compute. The implementation uses a bounded immutable zero-upload buffer and
retains both native resources until submission retirement. Zero-size operations
still undergo range and resource admission. Invalid ranges reject the entire
submission before execution; they do not silently truncate or partially clear.

Build `tests/d3d12_clear_readback.c` with the pinned Zig C compiler and
`-I vendor/webgpu-headers`; run the executable with the DLL path and one of
`full`, `partial`, `whole`, `zero`, `large`, `readback`, `recover`, `range`,
`size`, `zero-range`, or `destroyed`. The fixture independently checks all words,
including unchanged regions, early caller release, validation rejection followed
by valid reuse, and explicit device teardown. `large` crosses the bounded upload
chunk boundary. The existing automatic-layout fixture adds `clear` and
`clear-after` modes for copy/clear/compute/copy ordering with the same WGSL kernel.

| Contract | Vulkan / Metal source evidence | Windows D3D12 acceptance boundary |
| --- | --- | --- |
| Ordered buffer clear and copy | [Vulkan queue](src/native/queue/doe_queue_submit_vulkan.zig), [Metal queue](src/native/queue/doe_queue_submit_metal.zig) | Physical range, ordering and exact-readback checks in [clear fixture](tests/d3d12_clear_readback.c) |
| Invalid submission leaves prior data intact | [Shared admission](src/native/queue/doe_submission_validation.zig), [native fixture](tests/native_recorded_compute.c) | Physical misalignment, out-of-range and destroyed-buffer rejection; valid reuse after range rejection |
| Recorded scalar-storage compute | [Native compute fixture](tests/native_recorded_compute.c) | Dense group-zero scalar-storage fixtures above; other binding shapes and indirect dispatch unsupported |
| Texture transfers and mixed render/compute submission | Backend queue implementations | Unsupported in the Windows recorded compute/copy lane |
| Callbacks and device lifetime | [Callback contract](../../docs/native-callback-contract.md), [device contract](../../docs/native-device-lifecycle-contract.md) | Mapping, early release, explicit destroy and teardown exercised locally; complete callback/device-loss parity unverified |
| Application performance and browser ownership | Separate governed workloads and provider acceptance | Unverified; these correctness tests supply no speed, CPU-memory or browser win |

The comparison column identifies existing contracts and implementations, not a
fresh Vulkan or Metal hardware run. BEE cannot physically qualify Metal. The
Windows tests do not establish full WebGPU conformance or cross-backend parity.

## Windows indexed workgroup atomics

The HLSL emitter keeps indexed workgroup `atomicLoad` and `atomicStore`
operations attached to the actual `groupshared` array. Passing an atomic target
as an HLSL `inout` scalar loses that identity and is rejected by DXC. Helpers
specialized to the declared array preserve signedness and evaluate each index
once, including indices captured when a local pointer is created.

The [automatic-layout fixture](tests/d3d12_auto_layout_readback.c) adds `atomic`
and `atomic-signed` modes. They check signed/unsigned shared-array load/store,
barrier visibility, captured-pointer indexing after index mutation, exact
neighbor-sum output, early caller release and the retained invalid-clear guard.
The [HLSL regression](tests/core/hlsl_subsystem_test.zig) checks target identity
and index capture. This does not qualify all atomic operations, pointer shapes,
storage atomics or arbitrary workgroup programs.

The native CLI storage fixture also qualifies indexed shared-array `atomicAdd`,
`atomicSub`, `atomicMin`, `atomicMax`, `atomicAnd`, `atomicOr`, `atomicXor` and
`atomicExchange` for signed and unsigned elements. Target-specific helpers name
the actual `groupshared` array, preserve captured pointer indices and return the
pre-operation value. Independent old/new bit patterns include unsigned wrap,
signed comparisons and shared contention with a ticket count/mask/sum oracle.
This additional evidence belongs to the CLI consumer; it does not expand the
public-ABI fixture claim. Indexed shared-array `atomicCompareExchangeWeak` also
uses the semantic result struct and D3D12's strong `InterlockedCompareExchange`
primitive, which is permitted for weak CAS. Tests exercise signed/unsigned
old-value and success fields, mismatch and same-value replacement, captured
indices, operand evaluation once, neighbor visibility and one contended winner.
The no-spurious-failure oracle is specific to this strong target primitive;
it does not impose a stronger contract on other backends' weak primitives.
Other pointer shapes and storage atomics remain unqualified.

## Windows native CLI storage boundary

The native CLI admits group-0 scalar storage bindings in slots 0 through 7,
including sparse layouts, from WGSL with entry `main` and per-command completion. Read-only inputs use
SRVs and writable buffers use UAVs; shader access must match the declared layout.
Workgroup and dispatch dimensions are bounded by D3D12 limits and retained
per-buffer compiler obligations. Physical qualification exercises the scalar
path and the unchanged three-binding, two-dimensional matrix workload. Its [compute owner](src/backend/d3d12/d3d12_runtime_compute.zig)
emits the admitted analyzed module and enforces the retained compiler dispatch
preconditions. Opaque precompiled bound shaders, aggregate elements, binding
slots above 7, nonzero groups, aliases, nonzero binding offsets and deferred bound
dispatch remain unsupported.

The [storage owner](src/backend/d3d12/d3d12_kernel_buffers.zig) preserves buffer
identity across commands, caps total resident storage at the backend limit,
and caches ordered SRV/UAV plus dispatch-info CBV tables by exact handle/layout
identity. Sparse register numbers map to compact descriptor-table entries in
ascending binding order. Pipeline reuse checks the occupied slots as well as the
complete access/count layout.
Initialization zeroes new handles through checked GPU uploads; reusing a handle
does not reset its contents. Dependent dispatch transitions, checked queue
completion and real GPU readback preserve the command-graph output contract.
Shader preparation and measured submission have separate timing fields; the
submission counter records actual measured dispatch submissions.

`buffer_write` admits aligned, nonempty input and partial updates to these
command handles. New handles are zeroed before publication; existing buffers
cannot grow. Checked staged copies wait before reusing the upload allocation,
including writes crossing the staging chunk boundary. Initialization and payload
copies contribute their actual encode, completion and submission metrics; CPU
preparation remains separate. Deferred writes, unaligned byte ranges and resizing
remain unsupported.

Build `doe-runtime`, put the already-installed DXC on `PATH`, and run
`python tests/d3d12_cli_storage.py --runtime zig-out/bin/doe-zig-runtime.exe --out <new-evidence-directory>`
from this directory on Windows. The fixture runs the unchanged
[atomic workload](../../examples/workgroup_atomic_commands.json) with
`--validate-output-oracles`; omitting that flag does not check output. It also
uses independent command-graph hashes to check zero initialization, handle
rebinding, dependent repeats and pipeline reuse, followed by layout, dispatch
dimension and fixed-extent rejection/recovery cases. Nonzero seeds, partial
updates and chunked writes use independent CPU-derived full-buffer hashes;
write-created buffers and rejected-range recovery also check execution and
submission counts. The unchanged matrix command retains its shader, workgroup,
warmup and repeat count. Additional identity and dense seeded matrices check the
full output against independently computed exact f32 bytes, including input
reordering, handle reuse, scalar/matrix layout switching, rejected aliases and
access mismatches. Bounded control-flow cases also check that `continue` executes
the nearest loop's continuing clause, that nested loops do not inherit an outer
increment, and that `break if` still exits the loop through a switch/continue
edge. Explicit lexical blocks preserve shadowed and sibling local names, switch
cases own their local declarations, and loop-body locals remain visible to the
continuing clause. Colliding local names receive stable IR-based HLSL names, so
a continuing clause emitted at a shadowing `continue` site still references its
original variables. These checks exercise HLSL lowering on D3D12; other target
backends need their own physical qualification. Original failed traces remain
separate from accepted evidence. Sparse slot-3 dispatches, mixed read-only slot-1
and writable slot-7 buffers, JSON binding reordering and dense/sparse layout
switching retain exact output checks. Out-of-range slots, missing shader bindings
and aliases have separate status/submission rejection-recovery checks; the strict
output-oracle replay requires all commands in its graph to succeed.

This qualifies the exercised native CLI consumer only. A source-matched Windows
Dawn control, broader workload coverage, matched CPU/memory measurements and
the performance gates remain prerequisites for any speed or parity claim.

## Native callback delivery

The [bounded callback contract](../../docs/native-callback-contract.md) specifies
instance-owned futures, requested delivery modes, pending mapping state and resource
leases through foreign callbacks. The [ONNX Vulkan fixture and replay](../../bench/external-projects/onnx-vulkan-callbacks/README.md)
qualify the named paths with pinned Dawn controls and unchanged applications.
The [device lifecycle successor](../../docs/native-device-lifecycle-contract.md)
extends the bounded Vulkan evidence to descriptor loss and DeviceDestroy. Abandoned
instances, actual driver loss and generic callback conformance remain open; neither
report establishes a speed advantage.
