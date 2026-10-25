# Doe C migration: retained baseline and acceptance gates

## Scope and present state

This records the immutable inventory and the explicitly requested C migration.
C is selected; this is not mechanical translation or a completed rewrite. The
initial isolation branch preserved the retained implementation while owners were
built. The subsequent explicit direction is to integrate focused-checked source
progress into canonical main without force, rather than leave it on an unmerged
branch. Keep retained Zig, production-default selectors and historical receipts.
Source integration is not production promotion: authored, exercised, independently
accepted and physically qualified remain distinct. System installation and
external-machine operation require their own authorization; exposed or uncooled
hardware must not be used for physical qualification.

The immutable baseline is commit
`665475907494d2095a760596218b4c2d363cb436`. The
[machine-readable manifest](zig-inventory.json) includes every tracked `.zig`
blob at that revision, even when sparse checkout does not materialize the file:

- 809 files, 9,810,557 bytes, 222,409 lines.
- 628 handwritten production-role files: 155,898 lines, including inline tests.
- 137 handwritten test/suite/support files: 54,605 lines.
- 9 generated files: 1,804 lines, comprising 7 test suite roots and 2 production
  definitions. Including generated definitions gives 630 production-use paths.
- 1 build root: 1,746 lines.
- 34 other files: 8,356 lines, comprising 24 retained historical source/probe
  snapshots and 10 repository-only benchmark/tool sources.

The prospective [quality and 50% size target](QUALITY_TARGET.md) and
[common C guide](../../runtime/c/STYLE.md) govern new work without rewriting frozen
receipts. The headline ceiling is 77,949 lines, with a separate mandatory fair
implementation-only comparison. Inline-test spans and the final delivery closure
are not yet audited; no achieved reduction or full feature parity is claimed.

**The immutable baseline contains zero C replacements.** Current bounded-owner
results below do not turn partial compiler/runtime coverage into complete
production replacement. Production-default cutover and physical qualification
remain separate gates. This inventory itself establishes no GPU execution or
Vulkan, Metal, or D3D12 physical qualification. Host compilation, sanitizer passes, fixture output,
bytecode validation, and simulated completion never inherit that qualification.

## Current implementation workflow

The 2026-10-25 owner-authorized [instance capability amendment](../../runtime/c/instance/CAPABILITIES.md)
admits the timed-wait and multiple-device requirements used by ONNX consumers.
The public two-device copy/readback and finite-wait client passes on Apple M3
Metal, Radeon Vulkan and CPU Vulkan. This closes the descriptor admission gap;
backend qualification and production-switch gates remain open.

The unchanged six-job ONNX consumer now passes on the Radeon with the C native
library: callbacks, lifecycle, two application runs, native-disabled rejection
and corrupted-oracle rejection. Corrections preserve finite-boundary literal
rounding, map validation/loss delivery and synchronous mapped-range rejection.
The applications execute 123 WebGPU operators with 149 dispatch calls each;
cleanup checks pass. This is focused developer evidence, not full stage-5 or
physical-backend qualification. The C runs do not produce the Zig journal.
Regression routes are the compiler's numeric tests and the public native
`compute`, `map-oom` and `instance-devices` clients. The retained Zig ONNX
consumer also passes after restoring reflected types in automatic compute layouts.

The [C runtime CLI](../../runtime/c/runtime-cli/README.md) implements all 27
retained command kinds, including asynchronous diagnostics and real windowed
surface lifecycles. Its parser admits all 84 AMD Vulkan workload rows (81 unique
paths). Separately, 91 of 98 tracked example files parse; seven `buffer_load`
files enter through the normalized plan consumer instead. `check` and
`check-dawn` exercise matching artifacts and independent readbacks on CPU
Vulkan. Parser admission does not establish execution of every stream: the
retained `copy_protocol` stream has an invalid tight row pitch and rejects.
ProcessEvents/WaitAny strategies, finite wait bounds, deferred queue completion,
final draining and delayed callback ownership have focused consumer checks.

The three formerly failing Node integration files (`compute-program`,
`native-reflection`, `shader-semantics`) now pass against the integrated C
library on CPU Vulkan. The compiler supports invocation-private module storage,
`countOneBits`, `unpack4xU8` and `mix` through its shared typed target pipeline.
Private roots retain per-invocation initialization and helper mutation without
appearing as shared workgroup storage in reflection. Native post-loss invalid
objects pass CPU Vulkan and Apple M3 checks; Windows remains unverified.
Retained Vulkan surface generations and terminal cleanup pass focused CPU and
Radeon handoff/reconfiguration checks. These are scoped developer proofs.

The [C browser/offline WGSL adapter](../../runtime/c/compiler/README.md#browser-and-offline-wgsl-delivery)
builds without Zig. Its import-free Wasm artifact passes the existing package
interface tests and the unchanged particle application in Chrome 146 on Radeon
8060S: offline and worker output match the independent CPU oracle, all three
modes execute matched work, and lifecycle/cache/boundary controls pass. The
observed optimized mode is slower in that run; no performance benefit is claimed.
The browser owns GPU execution. This does not qualify the native C GPU backend
or complete broader WGSL coverage. The shipped browser artifact remains Zig
until the separately governed default switch.

### Open completion checklist

The lists below name known remaining work, not an exhaustive feature denominator
or a percentage-complete claim. A command, API export or passing test is not an
interchangeable unit of migration completion.

- Compiler: gather/gradient/bias image builtins; remaining memory barriers and
  workgroup-uniform loads; immediate storage; workgroup dimension/storage limits;
  TranslationInfo and retained compiler consumers; TSIR/CSL and the declared
  DXIL routes. Broader browser shader admission follows the same compiler gaps.
- Metal: native layer presentation, public query recording/resolve, partial
  sample masks/unclipped depth, and depth/stencil transfers.
- D3D12: native window presentation, public query recording/resolve, indexed and
  indirect whole-pass graphics, broader texture shapes/formats, and physical
  verification of current lifetime behavior.
- CLI and consumers: native backend-lane policy controls, required GPU timing,
  persistent pipeline cache, remaining argument/render/texture contracts,
  `runModule`, and platform package/helper routes.
- Delivery and qualification: C-only builds for every retained consumer and
  platform package; required independent and physical qualification; the audited
  source-size target and final production switch. Zig remains the default.

The requested workflow is broad, disjoint subsystem implementation in parallel,
with review and focused compile/behavior checks as code changes. Exhaustive
integrated qualification follows authoring, not every small port. This changes
sequencing, not correctness, ownership, physical-backend or promotion gates.
Authored/unqualified code is not an accepted replacement. Follow
[the C work guide](../../runtime/c/AGENTS.md), including the enforced 999-line
source/header ceiling, one semantic owner and recoverable removal of superseded
live helpers. Historical receipts and behavioral tests remain intact.

## Source-review corrections (2026-10-23)

This live ledger tracks the seven findings from the source review of `d7cf74fb`.
Developer checks below are not physical qualification or full feature closure.

1. **Operation-wide writable aliasing:** resource owner, `a147b360` and
   `45f8264d`. One usage classifier covers dispatch bindings/dynamic offsets and
   whole render-pass/bundle history. `resources/DEVELOPER_RECEIPT_SCOPES.json`
   records conflicting/disjoint ranges, overwritten setters, subresources and
   retirement checks. Follow-up review found stage-visibility false positives;
   `97fb777c` now filters aliases by shared stage while preserving independent
   whole-render history. Zero/disjoint/shared-stage buffer and texture controls
   pass in49 host suites and sanitizers. Independent Suby source recheck at
   `60f0ab17` closes the visibility finding; the separate exact graphics
   composition below adds developer execution evidence.
2. **Empty auto-layout holes:** resource/native owners, `45f8264d` and `c87e494f`.
   One required-group predicate normalizes ignored groups/offsets; explicit null
   unbind works. Native admission tests execute sparse group 2 and ignored holes.
   Follow-up explicit-null layout slots and valid trailing layout queries now use
   one lazy canonical empty-layout owner (`97fb777c`, `05444824`), including
   untouched-output OOM/retry and all valid device indices. Independent recheck
   found auto-created empty slots retained pipeline exclusivity. `f7a5383b`
   normalizes these before ownership claims; the reproduced auto-hole getter
   reused through explicit layouts now passes alongside NULL in56 host suites
   and sanitizers. Independent Suby source review at `d5b80542` closes the final
   auto-empty reuse defect, with all46 resource inputs matching.
3. **Typed failure propagation:** native owner, `c87e494f`. Layout allocation
   results are handled; one ABI status conversion preserves OOM versus internal
   execution failure versus validation. Real allocation/backend fault tests are
   recorded in `native/DEVELOPER_RECEIPT_ADMISSION.json`. Follow-up review found
   pending-work recovery still collapsed status. `05444824` separates terminal
   state from typed status; real submit/write/texture/work-done fault controls
   preserve Internal/OOM/Pending and unknown leases. Independent Suby source
   recheck at `60f0ab17` closes this finding; receipts remain exact-snapshot scoped.
4. **Duplicate target semantics:** text-target owner. Shared read-only value IDs,
   ordered access paths and runtime-tail identity now replace duplicate helpers in
   `compiler/targets/ir_view.h`. Malformed-IR sanitizer controls and both targets
   pass; `targets/DEVELOPER_RECEIPT_ARRAY_LENGTH.json` binds the checks. Target
   syntax, address spaces and intrinsics remain separate.
5. **Excess emitted helper closure:** text-target owner. `targets/closure.h` now
   selects reachable access roots, transfer types and modes, plus aggregate child
   and workgroup-initialization helpers. Duplicate HLSL call walking is removed;
   Metal also omits unrelated function/context resources for a selected entry.
   `DEVELOPER_RECEIPT_CLOSURE.json` binds positive/absence tests, malformed-graph
   sanitizers and unchanged emitted-operation controls. Independent recheck found
   shared call DAGs revisit completed functions exponentially. A25-level doubling
   graph reproduced bounded-work exhaustion. Explicit caller-owned completed
   function/type maps now pass both source emitters and malformed-graph sanitizers
   without higher work limits (`DEVELOPER_RECEIPT_DAG.json`). Independent Suby
   source review at `d5b80542` closes this defect, with all44 target inputs
   matching. The receipt pins an earlier core:15 of51 core inputs differ at
   the reviewed merge, so this is not merged-runtime qualification. This trims
   resource helper multiplication, not every unused declaration.
6. **Duplicate texture-copy footprint arithmetic:** resource/backend owners,
   `45f8264d` and `a6901a80`. Both call the small checked arithmetic leaf in
   `runtime/c/common`; 59,049 bounded independent arithmetic cases plus overflow
   publication checks pass. Format, pitch defaults and zero-copy rules remain
   with their respective callers. No generic utility framework was added.
7. **Raw image-format integers:** compiler owner, `fa61e2c1`. Existing named format
   constants replace raw values in `image.c`; unrelated enum domains stay distinct.

Fresh backend/pending-work review of `cbdee872` found three additional
source-level gaps, separate from the seven corrections above:
- **Stable Vulkan device identity:** corrected at source. Native device creation
  carries the selected device/driver UUID pair across independently owned contexts;
  changed order, missing/duplicate identities and software-class mismatch reject
  without substitution. Explicit CPU fallback searches CPU adapters rather than
  depending on ordinal zero. Exact-ordinal private callers retain their contract.
- **Native error detail:** corrected at source for command wait/poll/recover,
  settlement and Metal pipeline construction. Backend command owners retain the
  first operation/code; native transport copies bounded detail into owned callback
  messages. Rejected callback admission reports the failure before dropping its
  protecting lease. Destroy preserves unobserved receipts. Exact-message, changed-
  order and sanitizer controls:
  [identity/error receipt](../../runtime/c/native/DEVELOPER_RECEIPT_IDENTITY_ERRORS.json).
- **Unattended pending retirement:** corrected at source with the existing pending
  owner and one device progress lease. Nonblocking backend polls retain unknown
  work and retire terminal success/failure after all public handles disappear.
  Idle final release joins; worker startup fails before device publication. A
  bounded once-only loader reference retains provider code until process exit,
  independently of GPU/resource cleanup, so `dlclose` cannot strand the worker
  in unmapped instructions. CPU Vulkan release-all/startup/recovery/lifecycle
  controls and real Linux/macOS loader controls pass. Exact inputs and limits:
  [pending progress receipt](../../runtime/c/native/DEVELOPER_RECEIPT_PROGRESS.json).
These corrections are developer evidence, not physical recovery qualification.
A later review of public surfaces (`e517f23b4`) found a separate WSI release-all
retirement leak and acquisition-retry ordering gap. These are corrected with
reserved typed surface records on the same pending owner, canonical presentation/
acquisition retirement and surface-before-device cleanup. Cleanup-only readiness
is separate from command WorkDone. CPU lavapipe fence-gated release-all,
held-view, discard-failure, retry, pixel/readback and sanitizer controls are in the
[surface progress receipt](../../runtime/c/native/DEVELOPER_RECEIPT_SURFACE_PROGRESS.json).
This does not establish physical presentation qualification.

The additive [ABI11 HLSL facade](../../runtime/c/compiler/DEVELOPER_RECEIPT_HLSL_ABI11.json)
now carries compiler-owned entry/register metadata through the shader owner;
actual DXC outputs agree with its typed bindings. Existing ABI1–10 remain intact.
The additive [ABI13 contract](../../runtime/c/compiler/SPECIALIZATION.md) now
separates whole-module validation from selected-entry specialization, with typed
override values, concrete workgroup layouts and independent SPIR-V/MSL/HLSL
artifacts. The [shader owner](../../runtime/c/shader/README.md) retains immutable
module/source policy and exact specialization identity; native compute and graphics
consume the selected artifact through the same resource/backend owners. Ordinary
WGSL `select` uses one semantic owner with eager arguments on all three targets.
Focused public C and Node software-adapter controls exercise these paths; they do
not establish full language, independent numerical or physical qualification.
The [Windows SDK build](../../runtime/c/backends/d3d12/DEVELOPER_RECEIPT_WINDOWS_SDK.json)
compiles the private backend and links its clients. Public Windows native DLL
composition, D3D12 execution and physical qualification remain separate gates.

The [public D3D12 receipt](../../runtime/c/native/DEVELOPER_RECEIPT_D3D12_PUBLIC.json)
now binds one actual SDK-built native DLL and unchanged compute, uniform, alias
and command-retention consumers to CPU WARP and the same binaries on Miles.
The separate [Windows Node receipt](../../runtime/c/native/DEVELOPER_RECEIPT_NODE_WINDOWS.json)
binds an actual LLVM addon and unchanged public first kernel to WARP and the same
provider/addon on Miles, with an isolated developer installation. It does not
publish a platform package or promote production defaults.
The public native receipt preserves the failed build/control attempts, including the false-green abort
status and first bind-group rejection. Unexercised APIs, later source and broader
qualification remain separate.

The [Win32 host/loader receipt](../../runtime/c/platform/DEVELOPER_RECEIPT_WINDOWS_HOST.json)
separately binds actual SDK execution of shared C/C++ primitives, real thread
handles/deadlines, DLL unload and pinned-worker residency. It closes that bounded
host prerequisite at its recorded source; it does not qualify later public D3D12
source, native provider execution or GPU behavior.

The standalone [Windows compiler host receipt](../../runtime/c/compiler/DEVELOPER_RECEIPT_WINDOWS_HOST.json)
binds an actual SDK-built x64 DLL, canonical exports and four ordinary CPU
consumers to one source revision, retaining both earlier build/fixture failures.
It supplies bounded compiler host evidence, not full numerical acceptance,
native Windows runtime/CLI execution, later platform-owner changes or GPU
qualification. The first successful run did not record an SDK version.

Reusable compute programs now share the ordinary resource submission owner.
Unpublished preparation reserves a command buffer, publishes only after native
recording succeeds, and aborts after releasing failed native artifacts. Failed
preparation therefore preserves ordinary submission and host-replay fallback;
published consumption remains permanent, including release during unknown replay.
The [resource reservation controls](../../runtime/c/resources/DEVELOPER_RECEIPT_PROGRAM_RESERVATION.json)
and native `program-prepare` client cover rollback, callback reentry and terminal
cleanup. Metal host replay uses its selected backend rather than requiring Vulkan;
the [public Node control](../../runtime/c/native/DEVELOPER_RECEIPT_METAL_HOST_REPLAY.json)
records changed-input execution on one Apple device before the separate transaction
follow-up. GPU-recorded programs remain Vulkan-only. These developer checks do not
close full language coverage, public Metal graphics, public D3D12, browser adoption
or the production cutover gates.

The public Metal frontend now composes WGSL stage metadata, resource-owned
textures/views/samplers, whole render passes and typed copies through the existing
tagged backend and pending owner. A bounded [Apple M3 developer control](../../runtime/c/native/DEVELOPER_RECEIPT_METAL_PUBLIC_GRAPHICS.json)
checks offscreen pixels, early release, discarded attachment zeros and packed
texture uploads. Indirect/indexed guards, queries, depth/stencil transfers and
sampling, partial sample masks and presentation remain separate missing paths;
this slice is not full Metal qualification. Backend-specific format queries and
fragment-write admission must inspect only the selected payload arm.

The separate [M3 vertex receipt](../../runtime/c/native/DEVELOPER_RECEIPT_METAL_VERTEX_FORMATS.json)
binds exact public draw/readback for 33 selected formats, default component fill
and admission controls to its source. Host/SDK checks cover all 41 conversions;
the first host alignment failure is retained separately. The remaining formats,
other devices, later source and broader graphics qualification remain separate.

The additive [Metal ABI12](../../runtime/c/compiler/include/doe_compiler_metal.h)
replaces sparse-number slot arithmetic for new consumers with compiler-owned,
per-entry dense buffer/texture/sampler records. Shared helper bounds retain logical
resource identity through entry-local byte-length facts. Legacy ABIs retain their
layouts and mapping policy. One shader semantic policy carries explicit core
binding-number admission through every target and participates in artifact identity;
numeric labels do not raise resource or traversal budgets. Native compute/render
use one typed map and snapshot owner; `mixed-resources` checks the public sparse
four-group path and lifetime on its selected backend. The
[frozen mixed-resource receipt](../../runtime/c/native/DEVELOPER_RECEIPT_METAL_MIXED_COMPUTE.json)
binds actual Apple compute, the offscreen regression, and Node's unchanged first
kernel to the same C library; SDK compilation has separate scope.

The standalone compiler offers explicit standard uniform layout support while
retaining conservative defaults. Natural layout and one physical uniform-load
owner cover compact buffers without requiring a Vulkan optional layout feature;
the [public compiler controls](../../runtime/c/compiler/tests/test_uniform_standard.c)
and exact-range CPU consumer check small bindings, guarded dynamic access and
layout identity. Native shader compilation now explicitly selects this policy,
and WGSL language-feature discovery derives from that same function. The
[public native consumer](../../runtime/c/native/tests/uniform_standard.c) exercises
exact4/8/12/32/64-byte ranges, guarded dynamic access, aggregate/matrix layout,
wrong-index and wrong-layout controls, and `requires` on the CPU Vulkan adapter.
Standalone defaults remain conservative. The separate
[Apple uniform/alias receipt](../../runtime/c/native/DEVELOPER_RECEIPT_METAL_UNIFORM_ALIASES.json)
binds these uniform controls and shared-buffer alias execution to one Metal device,
retaining the first guardian failure and corrected guarded results. Windows
execution and broader physical qualification retain their own gates.

Private D3D12 now has [actual CPU WARP execution evidence](../../runtime/c/backends/d3d12/DEVELOPER_RECEIPT_WARP.json)
in addition to SDK compilation: explicit software selection, native compute,
readback, early release and terminal cleanup. The public consumer receipt above
is separate; broader public APIs, physical coverage and performance remain gates.
[Miles developer controls](../../runtime/c/backends/d3d12/DEVELOPER_RECEIPT_MILES_D3D12.json)
now bind the SDK-built artifacts to physical AMD compute, indirect dispatch,
uniforms, textures, sampled graphics/timestamps and indexed geometry/depth.
The isolated installation and exact driver evidence do not qualify the unfinished
public frontend, presentation or performance. Additional physical campaigns retain
their own authorization and qualification gates.

Guides: the common C style retains one parser/semantic/layout owner with multiple
explicit target adapters. Vulkan's owner guide now states serialized operations
and callback/lease boundaries; its recursive mutex is retained because finalizers
reenter. No arbitrary mutex removal or claimed system-level security change.
Native graphics evidence at `11937bf9` binds181 exact dependency inputs,
157/202 real exports, eleven normal clients and full compiler/native ASan/UBSan
composition. Actual software rendering covers sparse layouts, bundles, occlusion,
viewport/blend/scissor, typed errors and early release.45 pinned exports remain
absent; this is not157/202 of the whole migration or physical qualification.
Matrix ABI8 has separate exact24-byte-uniform software Vulkan execution and
31 source/validator controls, not blanket qualification of all later composition.
Metal graphics ownership at `74a329c5` has seven host/typed-call/sanitizer
controls through the existing completion owner; Apple SDK/device execution
remains pending. `d7455c73` adds actual software fragment demotion pixel checks
with explicit enabled Vulkan capability, not derivative/helper parity. All
physical hardware qualification remains pending.

The subsequent source checkpoint adds161/202 native exports (`a5528296`),
including real enabled/disabled fragment-demotion consumers;41 remain absent.
Scalar atomic ABI9 (`342fcf1b`) shares typed address paths and robust access;
`a82b3edd` executes all11 operations, signed/unsigned storage and workgroup
cases, plus the unchanged256-lane workgroup-atomic fixture through software
Vulkan. The initial composed semantic-frame failure was repaired by consolidating
call handling and deleting two superseded units, not raising the ceiling.
Metal matrix text lowering (`e0b9e9b9`) has exact finite emitted-operation host
controls and rejects unsupported atomic instructions explicitly; Apple compilation
and device execution remain unrun. Metal MSAA/aspect ownership (`db455842`) has
typed native-call controls only. Vulkan depth/stencil initialization and discarded
attachment zeroing have actual software readback (`a61d944a`); culled-face stencil
admission is corrected separately (`7a7206af`). Shared debug recording stacks
(`be355fdb`) reuse existing encoder/pass/bundle state and pass72 host suites with
sanitizers, without a duplicate command queue. These receipts bind their exact
inputs; no single receipt qualifies every subsequently composed revision.

The next coherent source increment includes173/202 actual native exports
(`cab70b9a`), with29 still absent. Its recorded ABI9 composition passes16 normal
clients and seven fully instrumented clients for real software4x resolve/masks/
coverage, vertex-only depth, robust storage/workgroup atomics and canonical debug
recording. Shared resource attachment signatures and full-pass retirement are
owned once (`259b0e4f`, `e8bfd7f8`); Vulkan depth-only drawing (`46075ee6`) uses no
fabricated fragment/color stage. Metal timestamps (`492968b3`) have typed-call/
lifetime controls, not Apple SDK/device execution. HLSL matrix lowering
(`74d79300`) represents WGSL columns as native HLSL rows and reverses linear
product operands, with a same-shaped wrong-order negative control.

Function-pointer lowering (`ded1d273`) has60 focused source/API checks,34 sanitizer
cases and55 exact actual software output words, preserving stack ceilings and
existing public layouts. Text targets explicitly reject unsupported pointers
(`4507da09`); pointer-plus-collective closures remain unsupported until sound
write-through uniformity summaries exist. The new [C build front door](../../runtime/c/README.md)
creates a source-bound relocatable Linux developer package and runs a no-device
C++ consumer, with no Zig or system installation. That package's host check binds
`cab70b9a`; it does not expand earlier GPU receipt identities. Windows/macOS
packaging, complete backend selection, presentation, remaining language/resource
families and physical qualification are still open. D3D12 and Metal integration
are active implementation lanes, not implicitly qualified by Linux checks.

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

The [eighth increment](../../runtime/c/compiler/MILESTONE_8_RESULTS.md) adds
stage-aware abstract/concrete scalar numerics, module/local constants and their
dependencies, constant attributes, conversions and guarded target operations.
All APIs use the same grammar, retaining actual storage/reflection limits rather
than old experimental rejection dialects. Independent acceptance covers 997
semantic cases and 839 historical cases, including 67 named new admissions.
The [updated ledger](corpus-coverage-m8.json) admits 23 paths / 21 unique sources
through the unchanged consumer, Vulkan 1.1 validation and CPU instruction
execution; 75 still reject. Fixed workspace/frame are 76,512/76,592 bytes.
Host floating-environment preservation is qualified on the tested Linux x86-64
GCC envelope, not universal GPU numerical behavior or production adoption.

The [ninth increment](../../runtime/c/compiler/MILESTONE_9_RESULTS.md) adds
module-scope scalar functions, real target calls/returns and selected typed math
builtins through that same pipeline. Independent semantic/native/format gates
pass. The [new ledger](corpus-coverage-m9.json) admits 33 paths / 31 unique sources;
65 still reject explicitly. Fixed workspace/frame are 119,408/119,520 bytes under
reviewed ceilings, with separate conservative combined-stack accounting. No
production owner or physical backend becomes qualified from these results.

The [tenth increment](../../runtime/c/compiler/MILESTONE_10_RESULTS.md) adds typed
aggregates, nested checked layouts/access paths, compact constants and additive
caller-owned reflection through the same compiler. The unchanged binary-only
consumer admits 37 of 98 paths, each independently validated/executed; 61 remain
explicit rejections in the [ledger](corpus-coverage-m10.json). Fixed workspace/frame
are 140,056/140,208 bytes within reviewed ceilings. Exact historical adapters,
three corrected candidate defects and first failures remain source-bound.
This does not qualify workgroup synchronization, physical execution, production
TranslationInfo/adoption or a production-default replacement.

The [eleventh increment](../../runtime/c/compiler/MILESTONE_11_RESULTS.md) adds
typed workgroup storage, zero initialization, workgroup barriers and transitive
source uniformity. Independent multi-invocation execution passes the full unchanged
100-iteration shared-memory witness and its frozen schedules. The
[new ledger](corpus-coverage-m11.json) distinguishes 52 freshly emitted/validated
paths from 43 independently executed paths, nine validation-only emissions and
46 explicit rejections. All 37 M10 target streams are unchanged. Native storage,
host-environment, atomicity and sanitizer gates pass within the recorded host
envelope. Workgroup runtime metadata/adoption, physical execution, broader
collectives and complete compiler ownership remain unqualified.

The [twelfth increment](../../runtime/c/compiler/MILESTONE_12_RESULTS.md) adds
complete admitted-type overloads for eleven scalar/vector numeric builtin families.
Independent semantic, native, sanitizer and full composed-stack gates pass after
correcting a real candidate-1 stack-budget failure without changing ceilings.
The [new ledger](corpus-coverage-m12.json) distinguishes 67 emitted/validated paths,
48 execution-qualified paths, 19 validation-only emissions and 31 rejections.
Target-byte identity carries forward 43 accepted M11 and five fully executed
new-source proofs. The AbstractInt dot interpretation and upstream disagreement
remain explicit. No production-default owner or physical backend is promoted.

The first [CPU runtime owner](../../runtime/c/futures/RESULTS.md) is independently
accepted as an isolated C library for future IDs and completion queues. It retains
issuance-boundary selection, callback reentry, actual condition waits and explicit
retirement, with tested cleanup failures and native C/C++ consumption. Payload
allocation, instance/device ownership and GPU completion remain separate owners.
This does not change the zero production-default replacement count.

The [default-off native integration](../../runtime/c/futures/integration/RESULTS.md)
now exercises this C queue through actual retained instance/callback consumers,
including independently source-built shared-library consumption, last-caller
release, reentry, worker publication and fault cleanup. It is a mixed-language
CPU owner adoption, not a production switch or backend qualification. C-only
sanitizer coverage does not cover retained Zig reference counts or allocators.

New Zig source is explicitly transitional, not omitted from the migration goal:
`runtime/zig/src/native/support/doe_future_owner.zig` selects the retained/C owner,
and `doe_future_c_adapter.zig` adapts real typed storage, leases and wait records.
Both production-use additions must eventually have an exercised C-caller route
or approved removal before the full Zig-free gate closes. The new
`test_suite_c_futures.zig` and `tests/c_futures_integration_test.zig` are test
infrastructure additions. None alters the immutable 809-file baseline inventory.

The [callback payload owner](../../runtime/c/callbacks/RESULTS.md) and its
[retained-consumer seam](../../runtime/c/callbacks/integration/CONTRACT.md) now pass
independent CPU/headless acceptance. C owns generic payload allocation,
publication and retirement; typed API deliverers and the accepted queue retain
their separate responsibilities. Actual owned RequestAdapter failure callbacks
exercise the existing Metal-unavailable path, not device creation or GPU completion.
Default selection remains Zig. The new `doe_callback_owner.zig` facade and
`doe_callback_c_adapter.zig` typed bridge remain production-use migration
obligations; `test_suite_c_callbacks.zig` and `tests/c_callbacks_integration_test.zig`
are transitional test infrastructure. No baseline inventory entry is erased.

The [native-instance owner](../../runtime/c/instance/RESULTS.md) now passes
independent Linux x86-64 ReleaseFast CPU/host acceptance. One C allocation owns
the sole atomic count and accepted queue; copied queue/lease views remain
non-owning. Actual retained consumers exercise last-reference callbacks,
reentry, failure cleanup and unconfigured surface retention through reversible
four-provider builds. Debug, GPU/device success, imported textures, configured
presentation, package/browser adoption and LeakSanitizer remain unqualified.
Native sanitizers cover seven C units, not retained Zig/system code. Default
selection remains Zig; `doe_instance_c_adapter.zig` and the selected test root
remain explicit transitional migration obligations, not erased baseline paths.
See the [build and reproduction instructions](../../runtime/c/instance/README.md)
and [independent evidence](../../runtime/c/instance/independent_tests/FINAL_REPORT.md).

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
