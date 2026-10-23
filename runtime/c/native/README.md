# Experimental C public WebGPU native frontend

Status: authored, strict-compiled and developer-exercised through the pinned
public `webgpu.h`. Not independently qualified, a complete WebGPU replacement,
a performance result or physical-GPU evidence. Production/default selection is
unchanged. Only the explicitly implemented symbols in `src/procedures.c` are
exported; unknown and unimplemented names return NULL from `wgpuGetProcAddress`.

## Backend selection and checked scope

Explicit Vulkan and Metal requests use their own tagged context, typed payloads
and finalizers within the same device/resource/pending-submission architecture.
Unsupported backends, including D3D12, report unavailable without fallback.
Undefined selects Vulkan on Linux and Metal on Apple within this experimental
provider only; production/default provider selection is unchanged.

Metal adapter/device identity, shared buffers, writes/maps, copy/fill and direct
compute are composed with the real Metal owner. The ABI10 compiler and shader
owner supply MSL text, selected entry spelling, sparse binding slots and runtime
size metadata. Native conversion neither parses WGSL nor substitutes SPIR-V.
The focused host test replaces only the native platform boundary with its existing
call ledger. It proves composition, ordering, errors and retained obligations,
not GPU copies, Apple SDK compilation, driver MSL compilation or Apple execution.
Linux's actual absent Metal loader also produces an explicit unavailable result.

Metal currently admits a chosen compute envelope: 256 total threads, at most
256 per axis, 65535 groups per axis and 256-byte buffer offsets. These are frontend
restrictions, not queried hardware claims. Actual buffer/threadgroup-memory and
per-axis limits further constrain admission; the backend checks the real
per-pipeline total-thread limit. Metal images, graphics, timestamps and indirect
commands remain uncomposed. Native Metal execution failures are not guessed to
mean device loss. Darwin recipes now select dylib, @rpath install identity, and an exact Mach-O
export list. Host dry-run/parser controls and actual Linux linking are checked;
Apple SDK compilation, Darwin execution and physical qualification remain unrun.

## Actual ownership and execution path

- Public instances alias the accepted C instance, including its future queue.
  Instance capability exports delegate to its conservative empty-feature/zero-limit
  contract, not inferred new feature support. Requests, map, queue, loss, compilation-info and async pipeline callbacks use
  accepted C callback payloads and leases. Device-loss obligations outlive device
  storage without retaining a device cycle; last external release clears the weak
  callback handle before destruction. Explicit destruction completes loss once.
- Adapters/devices have their sole native references here. Queue handles retain
  their device. Public buffers, shaders, layouts, groups, pipelines, encoders,
  passes and command buffers alias their typed C resource owners, not wrappers
  with duplicate references. Device-bound shader resources adopt the separately
  shareable immutable `doe_shader` artifact.
- Shader creation calls the C compiler for the selected backend target. Entry selection uses the actual entry
  reflection and its static resource closure. Layout checks include reflected
  minimum binding sizes, access kind, stage, resource/device identity and native
  limits. The Vulkan capability scanner admits Shader/Matrix, baseline 1D images and
  ImageQuery explicitly. The native context opts into demote when actually available;
  its exact SPIR-V extension/capability is admitted only when successfully enabled,
  never from an API-version guess. Other capabilities/extensions reject before pipeline
  creation. Extended storage formats and cube arrays remain disabled. Workgroup
  storage and dimensions use real backend limits.
- Compiler `src/spirv.c` owns guarded loads/stores: runtime array bounds use
  `OpArrayLength`, signed indices have a nonnegative check, invalid loads return
  zero and invalid stores do not execute. The Vulkan context also requires and
  enables robustBufferAccess. The frontend does not recreate source analysis or
  fabricate retained TranslationInfo extent/texture metadata. ABI6 owns guarded
  integer image coordinates/layers/mips/samples, with zero loads and discarded
  stores outside bounds. Native admission maps semantic format names, never
  retained model enum numbers.
- Resource commands retain their dependencies, seal on finish, validate map and
  destroyed state before single-use submit, and hold submission references until
  settlement. Vulkan commands separately own the actual native recording and
  completion obligations. Buffer mapping stages real coherent native contents;
  writable maps write back on unmap. No CPU compute emulation is used.
- Public textures/views/samplers alias the same resource owner. Images are fully
  zero-initialized at every mip/layer through actual native uploads or attachment
  clears before publication, including images without public CopyDst usage. Private native
  initialization commands use the same pending-submission owner and an explicit
  submission device lease. Packed queue writes and 256-byte-aligned public command
  copies retain their distinct canonical validation. Native views/samplers remain
  alive through the recorded resource and backend obligations.
- QueueSubmit currently performs a synchronous native wait/recovery. This is not
  a completed asynchronous producer lane or a latency/parity claim. Uncertain
  completion retains both native and resource obligations; later queue/map calls
  may retry recovery. If still unresolved, work-done publishes no future rather
  than reporting success. Releasing all caller handles does not free unresolved
  work; unattended recovery/liveness remains unfinished.
- TimestampQuery is opt-in at device creation. Generated backend WGSL is compiled
  by the same C compiler before a feature-enabled device is published. Query
  sets alias the resource owner; zero-count sets need no native pool. Query resolve
  records raw timestamps, exact calibrated GPU nanosecond conversion and the copy
  into the caller's destination before later commands. There is no production CPU
  postprocessing or raw-tick fallback. A terminal failed submission discards the
  possibly poisoned normalizer; later work creates a fresh one. Unknown completion
  retains its command-owned dependencies and prevents reuse.
- Genuine compiler-owned vertex/fragment entry reflection drives stage-specific selection,
  shared automatic binding layouts and interface admission. Single-color render
  or depth-only passes, direct draws, reusable bundles and occlusion queries lower through one
  ordered Vulkan pass span. The resource owner retains pass history, bindings,
  query identity and vertex ranges; native conversion owns no parallel recorder.
  Dynamic viewport/scissor/blend and culling use real backend state. Integer clear
  colors truncate and range-check before a defined C cast; ignored Load clears
  are not converted. Extra fragment outputs are ignored, while written targets,
  scalar/channel compatibility and source-alpha blending are checked.
- Depth32Float, Depth24Plus, Stencil8 and Depth24PlusStencil8 support attachment-only
  usage with actual depth tests/writes, stencil operations and read-only bundles.
  Plus formats use D32/D32S8 backing, not an implied optional public feature.
  The backend's sole per-aspect initialization owner clears undefined contents;
  depth/stencil Discard followed by Load reinitializes only missing aspects on GPU.
  Initialized companion aspects survive. Partial combined-aspect views remain
  unsupported; same-format standalone DepthOnly/StencilOnly views are admitted.
- Sample counts 1 and 4 use actual attachment storage, resolve, sample masks and
  alpha-to-coverage. A vertex-only depth pipeline needs neither a fabricated
  fragment shader nor a color target. Discarded multisample color is initialized
  by the existing backend aspect owner before a later Load.
- Indirect Vulkan commands consume GPU-produced argument bytes through backend-
  generated WGSL guards compiled by the existing C shader owner. Dispatch guards
  use the admitted device dimension limits; draw guards use exact bound vertex/
  instance spans and suppress nonzero firstInstance while its feature is disabled.
  Draws retain distinct scratch until retirement. Public Indirect usage gains only
  internal native CopySrc, not new public usage. No CPU argument readback or second
  recording engine is used. Failed terminal work discards private helpers; unknown
  completion retains them. Product-sensitive workgroup_index/global_invocation_index
  builtins remain compiler-rejected until reflected guard support exists.
- Indexed Vulkan draws (SetIndexBuffer, DrawIndexed, DrawIndexedIndirect on passes
  and bundles) use the resource owner's existing index state and packets: named
  Uint16/Uint32 format, format-size offset alignment, Index usage, exact bound
  range, strip/stripIndexFormat equality and direct firstIndex+indexCount range
  are validation errors there. Native lowering passes the exact bound range to the
  backend. Strip pipelines enable primitive restart (all-ones index); lists never
  do. Indexed indirect uses the same backend guard owner and distinct scratch: a
  GPU-read firstIndex+indexCount beyond the bound range, a wrapping sum, zero
  counts, instance-step overrun or nonzero firstInstance becomes a native no-op.
  Uint32 indices require the enabled native fullDrawIndexUint32 feature. Index
  values are never read on the host; vertex-step fetches through arbitrary GPU
  indices rely on enabled robustBufferAccess. Metal indexed drawing is absent.
- Viewports use the canonical requested-device bounds, including negative origins,
  outside-attachment viewports and zero extents. Scissors remain attachment-bound;
  zero viewport extent preserves native draw/query ordering with an empty scissor.
- Debug push/pop/insert validate canonical encoder/pass/bundle state and balance.
  Label bytes are optional diagnostics, not a second recorder or balance counter.
  ABI9 storage/workgroup atomics use compiler-owned robust access and initialization;
  no atomic type is reinterpreted by a frontend source parser.
- Device serialization covers mutable resources and selected backend state. Internal
  recursive acquisition permits resource-finalizer cleanup using the same lock.
  Foreign callbacks run only after unlocking, with required full-call leases.

## Intentionally unprovided or rejected

Surfaces remain absent. Vulkan indirect dispatch and nonindexed/indexed indirect
pass/bundle draws use real GPU guards; Metal graphics, indirect and indexed draws remain uncomposed.
Graphics
admits one color attachment and/or depth/stencil, sample counts 1 and 4, direct
triangle/line lists or strips and 32-bit float/signed/unsigned vertex attributes.
PointList target lowering, single-sample color Discard, dual-source blending,
vertex or fragment storage writes and fragment FragDepth/SampleIndex/SampleMask
remain explicitly rejected. WGSL discard requires actual enabled demote support;
unsupported devices reject that pipeline. Blend constants must be finite f32.
Texture format reinterpretation, depth copies/sampling/storage, multisampled
sampling/copies/storage, cube arrays and anisotropic sampling are rejected.
The baseline admits 18 named Vulkan
color formats, subject to actual device usage/format capabilities. Device
descriptors may request TimestampQuery when the actual adapter supports the
canonical GPU nanosecond normalizer. Other required features and required limits
reject rather than silently succeeding. Buffer, sampled/storage image and sampler layouts are
admitted; extensions, binding arrays,
immediates, specialization constants and nonbaseline shader capabilities reject.
ABI6 lacks exact texture/sampler pair-use reflection through helper parameters.
Selected layouts combining UnfilterableFloat with Filtering samplers therefore
reject explicitly; this is an unsupported analysis case, not full admission parity.
The image receipt predates canonical cross-binding usage admission. The separate
admission correction exercises the resource owner's whole-buffer read/write
conflicts, effective writable ranges and overlapping texture mip/layer identities.
Empty pipeline-layout slots are normalized by that owner and skipped by native
conversion, not required or validated as bound groups. Nonempty slots remain
required. Trailing GetBindGroupLayout indices below the device limit use the
resource owner's lazy canonical empty layout, with typed, retriable OOM. Native
recovery returns completion certainty separately from its actual status. Allocation
and native execution errors retain their typed public error
category rather than being relabeled as validation errors.
Descriptor labels and SetLabel use the existing label owner, including exact
length-delimited bytes and resource-finalizer cleanup. Failed WGSL modules retain
real compiler diagnostics for GetCompilationInfo. Compute and render pipeline construction
is synchronous; the async API delivers its actual result through the requested
callback mode, not a claimed background compiler. No blanket success stubs or
fallback provider resolution exist. The public advertised limits
cover the implemented compute/image/graphics path only and do not claim the WebGPU
Core feature level is complete; adapter/device creation remains experimental.
Metal image/graphics/timestamp composition, D3D12 admission, package adoption
and surfaces remain unfinished. Private backend checks do not qualify those public
integrations; the Metal compute bridge remains host-tested only.

## Reproduce from a coherent checkout

The Makefile invokes the compiler owner's canonical `shared` target for
`libdoe_compiler.so`, without parsing or duplicating its target source lists, then links `libwebgpu_doe_c.so` from C owners only. It writes
all outputs to /tmp by default. Existing GCC/G++ and Vulkan loader/ICD are needed;
no installation or machine-setting change is performed.

```
make -C runtime/c/native BUILD=/tmp/doe-c-native check \
  VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json
```

`check` requires a fallback CPU adapter and verifies the returned adapter type
is CPU; see the physical developer runs section for the GPU variant. Its ordinary
result is:

```
PASS public indexed draws: uint16/uint32 indices, firstIndex, signed base vertex, instance selection, offset index ranges, pass/bundle replay, uint16/uint32 strip primitive restart, 9 GPU-produced drawIndexedIndirect records with exact-end/out-of-range/wrapping/zero/firstInstance/instance-overrun guards, unchanged arguments, 13 typed validation controls and early releases; exact 192 pixels (CPU Vulkan only)
PASS public ABI9 atomics: storage accumulation, signed OOB suppression, compare-exchange result identity, fresh workgroup initialization across two groups and two dispatches, early releases (CPU Vulkan only)
PASS public debug markers: typed procedures, independent nested balance, parent lock, underflow, malformed strings and finished/unbalanced command/pass/bundle rejection (CPU Vulkan only)
PASS public MSAA: actual 4x color/depth, exact resolve pixels, mask0/zero-and-full alpha coverage, empty resolve, same-batch discard-zero and depth-only empty/query/bundle passes (CPU Vulkan only)
PASS public depth/stencil: zero publication, genuine vertex-only depth draw, depth ordering/read-only bundles, selected array/mip aspects, dynamic stencil, culled writes, same-batch discard/reinitialize preserving the other aspect, early releases and admission negatives (CPU Vulkan only)
PASS public discard: actual enabled feature, helper continuation, asymmetric pixel suppression, exact extension/capability gating and early releases (CPU Vulkan only)
PASS public graphics admission: stage/output identity, async modes/reentry, explicit empty vertex slot, zero-count range and bundle state reset
PASS valid empty WGSL has no compilation diagnostics; missing pipeline entry rejects
PASS public C graphics: genuine WGSL stage IO, sparse shared uniform, empty vertex slot, default fetched components, dynamic blend/scissor/culling, bundle replay, one native pass, integer clears, empty LOAD, real occlusion and early releases (CPU Vulkan only)
Explicit software-only WebGPU C path: SwiftShader Device (Subzero); not physical GPU qualification
PASS public ABI: compute/readback, OOB guards, dynamic offsets, cross-device rejection, one-shot submit, early releases, callback reentry and map cancellation
PASS C++ pinned typed procedure calls and unsupported-name rejection
PASS device loss modes/future/last-external-release/failed-creation, owned labels, async pipeline and real compilation diagnostics
PASS public textures: actual zero mips, packed upload, linear sampling, image OOB guards, storage readback, early releases, exact view defaults and filter/pair/copy negatives (CPU Vulkan only)
PASS public admission: sparse/trailing/null layout holes, null unbind, ignored destroyed resources, dynamic writable ranges, whole-buffer read/write conflicts and texture subresource aliases (CPU Vulkan only)
PASS public error types: resource allocation, automatic-layout allocation, compute/render backend OOM/native/validation failures, failed requested-feature device and successful recovery
PASS public timestamps: requested feature, zero/max/range/destroy admission, real pass/command writes, exact GPU nanoseconds, same-batch copy, scratch reuse and early query release (CPU Vulkan only)
PASS typed recovery: native/OOM/pending errors survive submit/write/texture/workdone, unknown leases retained, terminal cleanup and fresh GPU normalizer recovery
PASS every implemented procedure address matches; every unprovided name returns NULL
PASS exact exports: 182 implemented, 20 pinned unprovided, no private exports
```

A compute shader with two declared entries selects `transform`, runs 72
invocations on a 64-element runtime array, and checks all 64 readback values.
Checks also cover mapped-at-creation writeback, dynamic offsets, invalid dynamic
offset poisoning, readonly-map write rejection, wrong-device shader rejection,
one-shot command rejection, cancellation generation, duplicate wait records,
early caller releases and explicit recursive event pumping. C and C++ invoke
real returned procedures through the pinned typed function pointers. Lifecycle
checks cover all three loss modes, eight concurrent last-reference releases,
failed device creation, nested event pumping, label copying/removal, deferred
shader-release-safe diagnostics and success/failure async pipeline delivery. A
borrowed callback handle can acquire a new external reference after concurrent
last external release, without restoring destroyed state or weak loss identity.
Image checks execute filtering and storage instructions, robust OOB handling,
zeroed mips, packed uploads, readback and early releases; negatives cover float32
filterability, unresolved sampler pairing, destroyed views and copy-row alignment.
Admission controls require sparse group2 but not empty holes, execute disjoint
dynamic writable ranges, reject overlapping writable and whole-buffer readonly
conflicts, and distinguish overlapping from disjoint image mips. Failure controls
inject errors at actual resource allocation and Vulkan acquisition boundaries,
including partial auto-layout unwind and lazy trailing-layout OOM/retry with
unchanged device lease count. Timestamp checks compare actual GPU output with an
independent integer binary32-period oracle and separately observed raw query ticks,
including an unwritten zero, adjacent sentinel and same-batch copy. Recovery controls
obscure a real submission's completion at the backend boundary, preserve Internal,
OutOfMemory and Pending results, retain unknown leases, then prove terminal cleanup
and actual successful reconstruction of the GPU normalizer.

The preserved empty-WGSL reproduction now passes through the genuine ABI8 compiler:
zero-entry modules have no diagnostics, while pipeline entry selection still
rejects them. This control is part of ordinary `check`; historical timestamp
receipts retain the original failure rather than being rewritten. Graphics checks
also cover an asymmetric 64-pixel image, back-face culling, shared stage resources,
vertex default components, bundle state, real nonzero/clipped/empty/unwritten
occlusion results, typed integer clear boundaries and early caller releases.
Pipeline negatives exercise stage identity, missing/extra fragment outputs,
source-alpha channels, unsupported topology and canonical zero-count vertex ranges.

```
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json \
make -C runtime/c/native BUILD=/tmp/doe-c-native-asan \
  SANITIZE='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' check
```

That build instruments both C compiler and native runtime owners. LeakSanitizer
is separately unavailable in this execution environment under ptrace; disabling
leak detection does not claim leak qualification. `DEVELOPER_RECEIPT_IMAGES.json`
binds the image increment to immutable committed dependency8a3fd4c1 plus listed
native bytes, including a fresh canonical compiler build. An earlier live image
sanitizer build overlapped unfinished ABI7 compiler edits and is not source-bound
evidence. `DEVELOPER_RECEIPT_ADMISSION.json` separately binds the hole/alias/error
correction to the recorded resource snapshot and pinned compiler artifacts, not
to later concurrent graphics source. `DEVELOPER_RECEIPT_TIMESTAMPS.json` binds the
subsequent ABI7 timestamp/recovery/trailing-layout composition to committed
b4e39b0 plus its exact resource overlay, with fully instrumented compiler and
runtime controls and its then-failing empty-module control. The later ABI8 graphics
composition is bound separately in `DEVELOPER_RECEIPT_GRAPHICS.json`, not inferred
from those earlier checks. `DEVELOPER_RECEIPT_DEMOTE.json` then binds actual enabled
discard plus accepted-owner instance capability exports to committed11937bf9. `DEVELOPER_RECEIPT_LIFECYCLE.json` binds the preceding lifecycle work. The earlier
`DEVELOPER_RECEIPT.json` and source manifest remain the historical first compute
checkpoint, not evidence for changed bytes. An initial live-worktree build caught
unfinished parallel ABI6 enum edits; checkpoint testing instead materialized the
committed ABI5 compiler 54f0fb9f6f7202cb13d37e7440ea9d7e13e99b0f with the recorded
runtime snapshot. That development drift is not a published checkpoint result.

## Physical developer runs and holds

Do not run this on Miles or Suby while their possible disassembly/cooling hold
remains. Every adapter-using client selects its adapter through
`tests/adapter_class.h`: software (CPU fallback adapter) by default, or a
nonfallback GPU adapter with `DOE_C_TEST_ADAPTER=physical`. Each PASS line names
the class and device it ran on. `make check-physical` runs the same 19 clients on
the system ICDs (`VK_ICD_FILENAMES` unset):

```
make -C runtime/c/native BUILD=/tmp/doe-c-native check-physical
```

On 2026-10-23 all 18 passed on Jupiter: AMD Radeon 8060S Graphics (RADV
STRIX_HALO, vendor 0x1002 device 0x1586), Mesa 26.0.3-1ubuntu1 RADV, Vulkan 1.4.335,
Linux 7.0.0-38. After indexed drawing was added, all 19 clients (including
`indexed-render`) passed again on that same adapter/driver the same day. That is
developer evidence for these exact clients on that
adapter/driver: the oracles are the clients' own expected values, not frozen
independent workloads, and it is not Vulkan backend qualification, a performance
result, or evidence for any other adapter, Metal or D3D12. Qualification still
needs an independent hardware consumer with source-bound receipts.

The depth receipt binds an isolated ABI8 compiler/shader composition with exact
resource-view and Vulkan-initialization/cull overlays. Later shared ABI9/atomic,
optional-color, debug-marker and MSAA source is not implicitly exercised by that
receipt; each composition needs its own focused checks. Physical runs stay held.

`DEVELOPER_RECEIPT_MSAA.json` separately binds the ABI9 composition: all 16 normal
clients and seven fully instrumented compiler/runtime clients pass. It exercises
4x resolve, mask0, alpha0/1 coverage, discarded-color zeroing, genuine vertex-only
depth writes, independent debug-group balance, and robust atomic results across
two workgroups and two dispatches. Its immutable dependencies exclude later
pointer/ABI10 compiler changes and indirect work. The first atomic test failure
used a reserved WGSL identifier; the corrected test and original log are retained.
`COMPILER_RUNPATH` is overridable for staged builds; relocation is checked by the
separate package owner, not implied by these development-path executions.

`DEVELOPER_RECEIPT_BACKENDS.json` separately binds the tagged Vulkan/Metal bridge
to committed c1b4efc1 dependencies and its exact native source. Its normal check
adds explicit backend selection and the host Metal call ledger to the existing
Vulkan clients. Metal controls cover actual compiler-owned MSL metadata, selected
entry, static/dynamic offsets and size slots, asynchronous pipeline delivery,
per-pipeline thread limits, no atomic-to-SPIR-V fallback, changed adapter identity,
typed allocation/native failures and unknown-completion retention. The recorded
first copy assertion incorrectly expected the mock ledger to execute GPU commands;
the corrected control checks exact native calls and known initial readback bytes,
without adding production emulation. All real image/graphics execution in this
receipt is explicitly SwiftShader Vulkan, not Metal.

`DEVELOPER_RECEIPT_INDIRECT.json` binds the later public GPU-guarded indirect and
viewport composition independently. GPU producers generate valid, overlimit and
zero dispatch arguments and invalid vertex/instance draw spans. Public controls
check exact invocation counts, unchanged original argument words without public
CopySrc, eight indirect draw query outcomes, four viewport query outcomes, exact
64 pixels, bundle replay, stride-zero instances, early caller releases and typed
helper acquisition failures. Recovery checks keep helpers while completion is
unknown, then discard and recreate them after proven terminal failure. These are
software Vulkan results; Metal indirect and indexed rendering remain unsupported.

The public RenderPassMaxDrawCount chain converts its exact uint64 value into the
canonical resource budget. Explicit zero is not the omitted default; direct and
indirect no-op draws consume one, empty bundles consume none, and aggregate bundle
admission occurs before partial replay. Unknown or duplicate chain nodes invalidate
the existing encoder. `tests/draw_budget.c` checks those boundaries and real clears.

`make -C runtime/c/native check-platform` checks both link recipes and the exact
Mach-O symbol projection without claiming Apple compilation. On a supported Apple
build environment, `SHARED_PLATFORM=Darwin all` selects the dylib recipe;
`check-portable` runs host ABI/procedure checks. The full SwiftShader and GNU
linker-fault suite remains explicitly Linux-only, not silently replaced by a
narrower Darwin check. The compiler owns its corresponding canonical shared recipe
and numeric portability. See Apple's [run-path library documentation](https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/DynamicLibraries/100-Articles/RunpathDependentLibraries.html)
and [nm definition](https://github.com/apple-oss-distributions/cctools/blob/main/man/nm.1)
for the install identity and symbol-audit boundary.

`DEVELOPER_RECEIPT_BUDGET_PLATFORM.json` binds the exact three software clients
(normal and fully instrumented runtime/compiler), Linux host ABI/link checks and
Darwin recipe-only controls. It pins resource budget ownership to committed
28dbe11d and preserves the earlier compiler closure, excluding subsequent mutable
WSI, numeric and setter-admission changes. Presentation remains unprovided in
this checkpoint.
