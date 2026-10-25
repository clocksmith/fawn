# Experimental C Metal backend

This is a real, default-off native implementation in plain C. It dynamically
loads the macOS Metal/Foundation/Objective-C system libraries and invokes typed
Objective-C runtime methods. It does not compile/link Zig, embed an interpreter,
load an alternative backend or manufacture a successful device on other hosts.
No SDKs or dependencies were installed for this implementation.
On Windows the POSIX-only library/symbol loader is excluded at compilation;
`doe_mtl_native_open` retains its explicit unsupported result. Portable typed call
and resource owners still compile, with no fake POSIX APIs, Apple loader or
successful substitute provider.

## Implemented boundary

`include/doe_metal.h`, `include/doe_metal_texture.h` and
`include/doe_metal_render.h` and `include/doe_metal_query.h` expose a backend-private ABI for the shared
native/resource owner to adopt, not a replacement WebGPU API or reference authority:

- Owned default device, queue, shared/private buffers and compute pipelines.
- Exact-length UTF-8 MSL compilation, named entrypoint and compiler-resolved
  workgroup size; NSError code/message survives native compilation failure.
- Sparse buffer slots, nonzero offsets and bound byte sizes. The optional
  compiler-selected size slot receives the retained checked bridge's packed
  uint32 lengths. Missing slots are explicitly cleared. No guessed workgroups.
- Direct/repeated threadgroup dispatch, guarded GPU-generated indirect compute,
  buffer-region copy and byte-pattern fill.
  Every native operation uses the real Metal encoder and ends it before the next.
- Private 1D/2D/array/cube/3D and device-admitted 2D multisample images, explicit mip/layer views and swizzles,
  retained samplers, staging-buffer upload/readback and texture-region copies.
  43 uncompressed color layouts and 14 BC layouts are admitted; BC requires the
  native capability property. Layout facts are not format-feature qualification.
- Mixed compute uses the same dispatch and lifetime owner with separate sparse
  buffer, texture (128 slots) and sampler (16 slots) index spaces.
- Real MSL vertex/fragment pipeline creation, explicit per-stage resource slots,
  vertex layouts, per-target blending/write masks and depth/stencil state.
- Whole render passes with color/depth/stencil attachments, native load/clear/store,
  indexed/nonindexed and instanced direct draws, viewport/scissor, cull/winding,
  blend constants, depth bias/clamp, depth clip/clamp mode and stencil reference.
  Each draw restores its own depth mode. Clear-only passes are real
  native encodes. Pure and combined depth/stencil formats, sample-count-matched
  pipelines, native resolve/store-resolve and depth/stencil resolve filters are
  admitted with runtime capability gates. X24/X32 stencil views are view-only.
- Native boolean/counting visibility query configurations write to an explicitly
  retained buffer; contiguous query runs, offsets and capability limits are
  validated without another query-set or result-aggregation authority.
- Native timestamp-only counter-set discovery and owned sample buffers, explicit
  blit-boundary sampling, ordered standalone stage/blit writes, compute-stage
  begin/end pairs and GPU resolves into
  ordinary buffers. Actual runtime sampling capabilities gate each path. A timed
  empty dispatch still creates a real encoder, never manufactured timestamps.
- One-shot submit, nonblocking poll, explicitly blocking native wait, terminal
  error and unknown-result handling, and deterministic owned cleanup.

The intended loader target is 64-bit macOS, x86_64 or arm64. Other OS/architecture
builds explicitly return `DOE_MTL_UNSUPPORTED`. Missing system libraries/symbols
return `DOE_MTL_LOADER_UNAVAILABLE`; absent device/limit methods fail explicitly.
This is an implementation target, not a qualified support tuple.
Named vertex-format identities are checked against the actual SDK for both
macOS ABIs. Scalar 8/16-bit/half and BGRA declarations are available from macOS
10.13; the existing mandatory `maxBufferLength` device query is a macOS 10.14
API, and SDK controls target macOS 13. This does not establish GPU availability:
real render-pipeline creation must succeed before publication, preserving its
native error when a device rejects the descriptor. No format capability is
inferred from the enum declaration alone.

## Lifetime and composition

Calls for a context and its objects require caller serialization. Constructors
publish only on success. Resource/pipeline/command objects retain their creating
context, so the Objective-C runtime cannot unload while those objects live.

Metal command buffers deliberately use `commandBufferWithUnretainedReferences`,
as the retained owner does. Recording retains actual backend resources and pipelines before encoding. Native handles returned by accessors are borrowed and
must not be released, mutated or submitted independently. A shared resource
wrapper may adopt the backend pointer and finalize it using its matching release
function. The wrapper continues to own public destroy/map/usage policy.

A buffer, texture or timestamp sample buffer has one unresolved recording owner. Competing commands and host reads or
writes return BUSY until that recording is discarded or terminally observed.
Unsubmitted native recordings are discarded before their resource leases. A
recording failure poisons the entire recording, so earlier encoded operations
cannot accidentally submit as partial success. This conservative boundary does
not yet admit concurrent recorded use of one allocation or tuned streaming reuse.

Shared allocations start zeroed. Private allocations require a full fill/copy
before reads/bindings; initialization is speculative inside a command and is
published only on successful completion. Terminal GPU failure poisons all
participating contents. A shared allocation recovers only with a full host write;
a private allocation is replaced. Partial writes do not make failed output safe.

Private textures require full initialization of each selected mip/layer before
sampling, storage use or partial updates. A 3D mip is one whole volume; array
layers are separate subresources. Views retain their parent texture. A discarded
recording drops speculative initialization; only native success publishes it.
Texture error poisons the allocation, which must be replaced. The owner does not
infer complete storage writes from shader text or dispatch dimensions.

Texture staging row pitches are conservatively 256-byte aligned at admission.
Pitches beyond Metal's native format-scaled maximum lower to individual block
rows with tight block-byte-aligned native strides; offsets retain all original
row/image padding and the checked final-row footprint. Region origins
and interior extents respect format blocks; final edge blocks may be partial.
Array copies advance buffer image strides but pass zero native bytesPerImage for
each 2D slice, while volume copies pass the 3D stride. Padding is neither copied
nor declared initialized. Texture copies require identical formats. View format
reinterpretation is limited to opt-in linear/sRGB pairs; unsupported formats and
out-of-range layouts fail before native calls. Native driver creation may still
reject a format/usage combination. Upstream admission remains responsible for
shader access, filtering, sample type and per-device format feature compatibility.

Render pass validation checks every attachment and draw before native encoding;
then it leases pipelines, attachment views/parents, buffers, textures and samplers
under the existing command owner. No open encoder or second completion policy is
exposed. Every draw binds an explicit vertex/fragment snapshot, including clearing
absent high slots. Pipeline construction shares the existing exact-span MSL
function builder; render and compute share buffer/image binding checks.

Attachment LOAD requires initialized contents. CLEAR plus STORE establishes the
whole selected aspect of a mip/layer; STORE after LOAD preserves that aspect.
Each subresource carries separate color/depth/stencil bits, including speculative
recording copies. Source STORE and destination RESOLVE publish independently.
DISCARD never proves complete raster coverage. Combined source/resolve attachments
require both aspects of the same subresource explicitly represented, so no
implicit preservation of an omitted aspect is assumed. Equivalent combined views
use one native attachment identity while every wrapper keeps its ordinary lease.
Combined-format sampling checks depth availability; X24/X32 views check stencil.
Initializing an X24/X32 sampled view creates legal combined-parent attachment
views for its selected mip/layers. Missing aspects clear to zero while initialized
companions LOAD/STORE; temporary views join the ordinary command leases, and only
terminal success publishes their speculative initialization. X formats never
become render attachments.
BC, ETC2/EAC and all fourteen ASTC LDR block shapes (linear/sRGB) use the same
transfer and lifetime owners. ETC2/EAC and ASTC require the selected device's
Apple2 family support; sliced ASTC 3D additionally requires Apple3. No CPU decode
or shader substitution is used. The new enum identities have source provenance
and SDK assertions; host checks do not establish Apple SDK or physical support.
BC/ASTC transfers admit block-rounded mip extents and sliced 3D through the existing
texture owner. Buffer/image native calls clamp only their edge texel extents;
buffer layouts and texture/texture copies retain whole physical blocks. Array
image strides and 3D depth coordinates remain distinct.
Depth/stencil staging transfers select the canonical depth or stencil aspect and
native blit option; successful uploads publish only that aspect in the same
subresource owner. Combined-format whole-buffer transfers reject. Depth/stencil
storage and multisample staging transfers remain unsupported.

`doe_mtl_commands_initialize_attachment` initializes a selected 2D/array/cube/MS
view range through real clear-only passes. It prepares every selected mip/layer
view before encoding, clears missing aspects to zero, and LOAD/STOREs initialized
companions. Repeating it on an initialized recording is a no-op. It consults the
same speculative bits as ordinary rendering, so Discard, abandonment, unknown
completion and terminal failure keep their existing ownership/publication rules.

Sample counts are queried with supportsTextureSampleCount. Published GPU-family
facts conservatively gate float32 blending and RG11B10 rendering, while
[supports32BitFloatFiltering](https://developer.apple.com/documentation/metal/mtldevice/supports32bitfloatfiltering)
is queried independently. Native format facts do not bypass frontend feature
enablement or required store/resolve composition. Family facts also gate arrays,
combined store/resolve, separate depth/stencil,
depth resolve, stencil resolve and counting visibility. Depth24Stencil8 uses its
native property gate and is never assumed on Apple silicon. These gates are not
per-format hardware qualification; native/frontend format admission still applies.

Logical empty viewports/scissors remain real draws: native raster state uses an
empty [scissor](https://developer.apple.com/documentation/metal/mtlrendercommandencoder/setscissorrect(_:))
while vertex effects and visibility ordering survive. Alpha-to-coverage uses the
native pipeline descriptor's typed BOOL property. An optional sample mask is
admitted only when all active samples remain enabled; partial masks return
UNSUPPORTED before allocation/native calls. This layer does not rewrite MSL to
manufacture a shader sample-mask output or substitute a scissor for coverage.

Visibility output uses initialized ordinary buffer storage and stays BUSY until
completion. Each offset is 8-byte aligned and belongs to one contiguous run of
draws in a pass. Consecutive draws share one native visibility configuration;
disjoint reuse or changing mode at the same offset fails before native encoding.
The shared query owner remains responsible for reset, logical aggregation and
public query-set policy. This backend never manufactures a visibility value.

Vertex layouts and direct/instance fetch ranges are checked. Legacy direct indexed
draws retain their explicit upstream fetch-proof contract. The typed guarded path
normalizes private GPU argument scratch and scans index windows before the whole
pass, deriving capacities from the same admitted attributes/layouts. Its V2 record
uses low/high capacity words; uint16 prefix/tail windows use initialized physical
padding without widening logical ranges. Atomic cancellation suppresses unsafe
fetches while valid caller indices/arguments stay unchanged. Common WGSL, SDK and
call-ledger controls do not establish physical indexed rendering.

Only native Completed or Error status retires obligations. Pending/unknown
command release returns BUSY without consuming the handle. Callers must keep the
command, observe again, and release it after terminal status. A blocking wait
returning nonterminal status remains unknown. No timeout/cancel success is
invented; shared async/future owners may build operational deadlines over poll.
The first terminal NSError operation, code and diagnostic text are copied into
that same command failure record before the native pool drains and leases retire.
Subsequent observations preserve the record even if the native error changes.

Timestamp indices share the same speculative/completion owner. Resolve requires
all selected samples to have been encoded or previously completed; no unwritten
sample becomes zero. Strict sampling writes an index once per recording. The explicit ordered
boundary first signals and waits for a native shared event, permitting later
passes to overwrite the index while preserving earlier GPU resolve snapshots.
Its stage path uses a leased four-byte native blit so the encoder executes.
GPU failure poisons the sample buffer. Public unwritten-query zero-fill remains
upstream, using this owner's canonical availability observation.
The explicit capacity4096 is an owner allocation bound,
not a discovered hardware maximum; native creation can reject smaller requests.
The timestamp-only counter set is checked by its exported native name and exact
single-counter layout. Resolve produces native uint64 CounterResultTimestamp
records; UINT64_MAX denotes Metal CounterErrorValue. No CPU values, nanosecond
normalization, Mach-timebase assumptions or successful timing claims are invented.
Paired sampleTimestamps CPU values are nanoseconds; their delta divided by the
GPU clock delta supplies the measured binary32 period. Zero/nonmonotonic baselines
do not expose a period. The public frontend compiles the shared integer-limb
conversion and keeps native CounterErrorValue unchanged.

## Concrete native composition prerequisites

The [C native frontend](../../native/README.md) now has explicit tagged
Vulkan/Metal compute selection, compiler-owned MSL stage artifacts and a Darwin
link recipe. Those sources and their focused host controls do not implicitly
qualify this backend or the public frontend on an Apple device. Integration still
must preserve these seams:

- The existing native device/resource identities adopt Metal objects and use
  their matching release functions. Do not add another reference authority.
- Shader artifacts supply exact MSL bytes, emitted symbols, stage/workgroup facts
  and flattened resource/size slots. Raw WGSL names and binding numbers are not
  substitutes, and MSL compilation alone does not prove the WGSL lowering.
- Native resource/command translation must map explicit Metal enums, format
  admission and stage slots, group a complete render pass before encoding, and
  retain the existing resource snapshots. Queue/future handling polls this command
  owner; unknown completion cannot release payloads or publish map/query results.
- This owner's `Makefile` remains its source-list authority. Darwin public
  packaging, full frontend execution and graphics/query/presentation wiring
  have their own native/shared acceptance; backend-only readback is not that proof.

Guarded indirect compute consumes the typed contract in
[doe_metal_indirect.h](include/doe_metal_indirect.h). The caller compiles the exact
shared `doe_dispatch_guard_source` for the resolved device bounds and supplies its
compiler-selected MSL entry, scratch binding and size slot. This backend does not
interpret source or accept an unchecked native dispatch request. Each recording
GPU-copies exactly three uint32 argument words into private scratch, runs that
validated guard and dispatches using the actual Metal indirect-buffer selector.
Caller arguments remain unchanged; no host contents query supplies counts.

The guard's PSO, target PSO, scratch and all buffer/image/sampler bindings use the
existing command resource leases. Failed recording poisons the entire unsubmitted
command; pending/unknown retains it. Native preparation compiles WGSL to owned
MSL outside the device mutex, then creates MTLLibrary/PSO under the existing
private context serialization and publishes one lazy device guard only while the
device remains live. Immutable duplicate source artifacts release outside locks;
there is no second queue or scratch cache. The current compiler supplies actual
`threadgroups_per_grid`, including indirect dispatch, rather than host-invented
`num_workgroups` values. Host call-order/lifetime controls and actual SDK selector
compilation do not establish GPU execution or physical qualification.

## Scope still outside this increment

ETC/ASTC images, surfaces, render-stage counter attachment mapping,
external textures, shared-event notifications, async callbacks, library/binary
caches, streamed encoder reuse, prepared-program
caches and full native/WebGPU consumer wiring remain in their retained owners.
Indexed/indirect draws require the typed common normalizer/scan guard described
above. Compiler/native owners admit shader features, usage and exact layout before this API;
it is not a standalone validation boundary for untrusted MSL. No exception bridge
is introduced: nil/NSError/status failures are handled, but Objective-C language
exceptions are not caught by this plain-C adapter.

## ABI provenance

`ABI_PROVENANCE.json` binds the retained bridge/native compute/completion sources
and render sources plus primary Apple references used for selectors, storage-mode constants,
Objective-C function-pointer calls, ownership and threadgroup semantics.
`src/objc_abi.h` contains only explicit method signatures and MTLSize/NSRange
records plus the explicit four-byte swizzle record. x86_64 uses the struct-return dispatcher for the three-word device
threadgroup limit; arm64 uses ordinary `objc_msgSend`. No speculative opaque
structure storage or C++ runtime is involved.

The host compiles both source branches and tests exact typed call arguments,
including sparse identities, offsets, size-table bytes, dimensions and native
error codes. Those checks do not validate against Apple SDK declarations or
exercise Apple's Objective-C dispatcher or rasterizer. `sdk-abi` separately
compares native layouts, scalar method ABI and selected enum identities against
the installed SDK, compiling every backend source for actual arm64 and x86_64
macOS targets. The explicit native smoke separately exercises the real dispatcher
and device. Neither substitutes for complete native consumer or graphics parity.

## Focused checks

From this directory:

```
make all check abi-compile probe
make sanitize
```

On an Apple SDK host, `make sdk-abi` is compile-only and creates no device.
`make native-check` is an explicit physical operation, excluded from `check` and
`all`; it requires an authorized healthy Mac. Its child process is limited to
20 seconds and polls completion for at most five seconds. The test records a
private-buffer fill/copy, one 64-invocation MSL compute dispatch, shared readback,
exact expected values and release of caller references before submission. On
unknown completion it does not explicitly destroy outstanding owners. This is a
bounded backend correctness smoke, not WGSL compiler/public WebGPU integration,
graphics/presentation, performance qualification or a production-default change.

The [Apple developer receipt](DEVELOPER_RECEIPT_APPLE_COMPUTE.json) binds the
2026-10-23 Apple clang/macOS SDK source checks and one real Apple M3 backend
compute/readback run. It also records the reproduced completion-detail defect,
its first-error preservation controls and the actual sanitizer boundary. All
12 ordinary host executables pass, and the two affected completion/lifetime
controls pass with UBSan. The combined ASan/UBSan binaries compile but the first
executable hangs in AddressSanitizer initialization before `main`; its captured
stack and termination are retained rather than called a pass. Earlier Linux
sanitizer receipts remain scoped to their recorded inputs and platform.

`make native-graphics-check` is another explicitly authorized, healthy-host-only
operation, also excluded from `all`/`check`, with the same 20-second process and
five-second completion bounds. Its 8x8 offscreen native-MSL consumer initializes
a four-sample attachment, draws/resolves a red triangle, and proves empty viewport
and scissor draws still perform vertex storage effects without changing pixels.
It releases caller pipeline/view/texture/context references before submission.
Alpha-to-coverage is enabled with opaque alpha1; fractional coverage behavior,
public WGSL/graphics composition, presentation and broader hardware qualification
are not established by this smoke.

The [graphics prerequisites receipt](DEVELOPER_RECEIPT_GRAPHICS_PREREQUISITES.json)
binds the actual Apple M3 run, all13 current host controls and both real SDK ABI
targets. Its initializer controls cover selected mip/layer ranges, partial-aspect
Discard preservation, a failed second view preparation before any encoder,
abandonment, unknown completion and terminal poisoning. The retained terminal
artifact distinguishes its first fixture assertion from the corrected host pass.

Artifacts default to `$(DOE_C_TMP)/metal`, a per-checkout root from [build-root.mk](../../build-root.mk). `check` runs the owned native-call fault
and lifetime controls plus typed message-call controls. It does not interpret or
execute a shader. On Linux the real public constructor's probe must reject Metal.
`abi-compile` is syntax/type compilation of source branches, not cross-compilation
or SDK verification. No target binary is executed under forged host macros.

In this hosted environment LeakSanitizer fails under ptrace. The initial failure
is retained in `TERMINAL_RECEIPT.txt`; `ASAN_OPTIONS=detect_leaks=0 make sanitize`
passes AddressSanitizer and UndefinedBehaviorSanitizer with leak scanning disabled.
Owned native allocations/releases are still checked by the call ledger. Run leak
scanning in a supported environment before qualification.

`TIMESTAMP_SOURCE_INPUTS.sha256` preserves the sources/tests/build/documents
exercised by `DEVELOPER_RECEIPT_TIMESTAMPS.json` at commit `492968b3`, before
the device-limit addition below. Verify it at that historical revision. Multisample evidence (`MULTISAMPLE_SOURCE_INPUTS.sha256`,
`DEVELOPER_RECEIPT_MULTISAMPLE.json`, `MULTISAMPLE_TERMINAL_RECEIPT.txt`) remains
unchanged historical evidence for `db455842`. Graphics evidence (`RENDER_SOURCE_INPUTS.sha256`, `DEVELOPER_RECEIPT_RENDER.json`,
`RENDER_TERMINAL_RECEIPT.txt`) is unchanged historical evidence for commit `74a329c5`.
Image evidence (`IMAGE_SOURCE_INPUTS.sha256`, `DEVELOPER_RECEIPT_IMAGES.json`,
`IMAGE_TERMINAL_RECEIPT.txt`) is preserved unchanged and binds commit `f6cd88b1`,
not the changed graphics-capable sources. The earlier `SOURCE_INPUTS.sha256`, `DEVELOPER_RECEIPT.json` and
`TERMINAL_RECEIPT.txt` are preserved historical compute-slice evidence, bound to
commit `33bf0b04`, not to the changed image-capable sources. Receipt consumer: the C migration lead's integration decision,
not physical support or performance promotion. No source-reduction or speedup
claim is made; complete retained delivery burden is not measured here.

The timestamp terminal receipt preserves its first compile failure and successful
rerun; `TIMESTAMP_FIRST_INPUTS.sha256` binds those failed-attempt inputs, not current
sources. The multisample terminal receipt preserves its exact failed controls;
`MULTISAMPLE_FIRST_INPUTS.sha256` records the initial failed compile inputs, not
the final source set. Later fixture failures and their fixes are also retained.

## Native device limits

Device creation now also requires and reads `maxThreadgroupMemoryLength`, alongside
`maxBufferLength` and all three `maxThreadsPerThreadgroup` axes. The one typed
property reader publishes these facts only when all required selectors and values
are valid. It does not invent a per-device aggregate-thread limit: the actual
pipeline's `maxTotalThreadsPerThreadgroup` remains a separate dispatch constraint.
Upstream public limits may be deliberately smaller, but must be labelled as an
admitted policy rather than queried hardware capability.

`make check sanitize abi-compile` includes missing selectors, zero limits,
distinct axes, non-narrowed buffer sizes and unchanged-output controls. The final
checks and exact source hashes are in `DEVELOPER_RECEIPT_LIMITS.json`. Typed host
calls and both syntax branches are not Apple SDK or physical-device qualification.
The property's official declaration is documented by Apple at
https://developer.apple.com/documentation/metal/mtldevice/maxthreadgroupmemorylength?language=objc .

Texture swizzle capability currently follows the published Apple2 or Mac2 family floor
([Apple capabilities table](https://developer.apple.com/metal/capabilities/)).
Nonidentity backend views reject without that capability; identity views remain
valid. Host controls cover the capability and descriptor paths, not GPU pixels.

### Explicit persistent pipeline archives

`doe_metal_cache.h` configures an optional `MTLBinaryArchive` on the existing
context. A nonempty directory explicitly opts into native directory creation and
`doe_pipeline_archive_v1.metallib`; no default path or environment mutation is
introduced. Missing, incompatible or corrupt archives fall back to an empty
archive. Configuration failure preserves the previously owned archive. Disabling
releases it without writing; explicit flush and context teardown serialize dirty
archives, with teardown failures remaining best effort.

Both compute and render constructors attach the archive to the **actual native
pipeline descriptor**, including the newly compiled functions and complete render
state. Metal performs compatibility matching; there is no entry-name-only shader
cache. Source-library compilation is not skipped. Archive pipeline failures retry
the ordinary native constructor, and optional addition failures do not invalidate
a successfully created pipeline. Reported counters describe load/add/serialization
operations, not cache hits or performance improvements.

`test_objc_archive` and `test_objc_render_pipeline` cover typed selector arguments,
load/miss/replacement, descriptor attachment, same-descriptor render fallback,
changed-source compilation, copied errors and reference cleanup. These host
controls do not qualify filesystem persistence, Apple SDK ABI, physical hardware,
or binary cache hits. The native public extension is documented in
`../../native/include/doe_native_pipeline_cache.h`.

Primary native contracts:
- [Apple binary archive workflow](https://developer.apple.com/documentation/metal/creating-binary-archives-from-device-built-pipeline-state-objects?language=objc)
- [Archive serialization](https://developer.apple.com/documentation/metal/mtlbinaryarchive/serialize%28to%3A%29?language=objc)
- [Compute descriptor construction](https://developer.apple.com/documentation/metal/mtldevice/makecomputepipelinestate%28descriptor%3Aoptions%3Areflection%3A%29?language=objc)


### Explicit queue timeline preference

Before publishing a per-device context, `doe_mtl_context_prefer_timeline` may
create an owned `MTLSharedEvent`. Unsupported event APIs retain ordinary command
completion; allocation/native errors remain failures. Submissions encode a
monotonic bounded `encodeSignalEvent:value:` after all encoders and before the
existing commit. The same command/context leases retain the event. Completion
requires its signal value and ordinary terminal command status: an event alone
cannot hide an NSError, release a drawable early, or retire uncertain resources.
No event wait spins, callback queue or alternate command engine is introduced.
Defaults do not request events. Counters cannot wrap or submit after exhaustion.

This uses Apple's [shared-event signal value](https://developer.apple.com/documentation/metal/mtlsharedevent/signaledvalue)
and [GPU/CPU event ordering](https://developer.apple.com/documentation/metal/synchronizing-events-between-a-gpu-and-the-cpu)
contracts and the existing timestamp owner's typed event-call ABI. `check-timeline`
checks native call arguments, terminal-error authority, missing signal proof,
overflow-before-commit and retained ownership. These are host-call controls, not
an Apple SDK build, physical-Metal execution, performance or cutover claim.

### External IOSurface/CoreVideo planes

`doe_metal_external.h` imports completed IOSurface or IOSurface-backed
CVPixelBuffer storage into the existing texture owner. Packed RGBA8/BGRA8 and
8-bit two-plane 420v/420f use actual `newTextureWithDescriptor:iosurface:plane:`
calls and canonical Metal formats (RG8Unorm is 30, not the retained bridge's
R16Float value 25). Every plane has ordinary view/command/context leases;
partial construction rolls back, and pending/unknown work retains its storage.
The caller synchronizes and preserves external producer contents through those
leases. No second command queue, resource refcount or fabricated GPU result is
introduced.

`make check-external` exercises typed call identities/arguments, packed and NV12
shapes, partial failure, untouched outputs, and normal command retirement using
host call ledgers. `sdk-abi` includes the new format and selector signatures;
it still needs an Apple SDK. These checks are not physical Metal import/video
qualification, and the import primitive performs no color-space conversion.
