# C resource and recorded-command ownership

Status: authored and developer exercised, not independently qualified. Retained
Zig remains authoritative until integrated qualification. No production/default
cutover, physical GPU, performance or complete WebGPU parity claim is made here.

## Owner and use

`include/doe_resources.h` is the typed internal C seam;
`include/doe_resources_extended.h` adds query, external-texture and render owners.
Allocation-free buffer/view/sampler preflight shares constructor admission.
Native creators pass real storage and its finalizer. Success alone adopts the
native handle; failure preserves ownership and output. Borrowing acquires no
lease. Every object retains its creating device through one common object base.
Immutable compiler artifacts have a distinct device-bound shader identity, not
an alias of device-free compiled data or a second count for one WebGPU object.

- `objects.c` and `accessors.c`: native/device lifetime, checked buffer ranges,
  shader identity, map modes, span and stale-generation admission.
- `formats.c`, `textures.c`, `texture_copy.c`: named format semantics, immutable
  textures/views/samplers and shared checked block/mip/row/image copy geometry;
  compressed textures have whole blocks at mip 0; render attachments need a
  WebGPU-renderable color format, multisampling a format that supports it and
  a resolve target a resolvable one (`render_targets.c`, the plain color formats table); view formats may differ only
  in srgb-ness (RGBA8, BGRA8, BC1, BC2, BC3, BC7). Copy validation modes:
  WEBGPU (buffer copies), QUEUE (writeTexture: no 256-byte or block alignment of
  bytesPerRow and offset; `row_bytes`/`rows` let the caller repack) and
  NATIVE (internal uploads that may end a partial block at the edge).
  The checked format/aspect sample-class and named channel-count functions serve
  native shader/clear scalar conversion; binding admission uses the same owner, including stencil-only
  combined formats. Baseline class is separate from optional feature promotion,
  native support and attachment renderability. Invalid inputs preserve output.
- `bindings.c`: canonical layouts, exclusive automatic-layout identity, retained
  groups/pipeline layouts, dynamic binding ranges and native pipeline leases.
  Binding numbers are sparse below `DOE_RESOURCE_MAX_BINDING_NUMBER` (1000, the
  WebGPU maxBindingsPerBindGroup minimum): layouts and groups store exactly their
  entries sorted by binding, never arrays indexed by binding number, and
  `doe_resource_bind_group_resolve_binding` finds one by binary search after
  `doe_resource_bind_group_check` validated the whole group. Layout and pipeline
  layout creation enforce the WebGPU binding slot limits (per-stage counts, an
  external texture counting 4 sampled textures, 1 sampler and 1 uniform buffer,
  dynamic uniform/storage counts, bind group count) from `config.slot_limits`,
  zero selecting each default; one group holds at most
  `DOE_RESOURCE_MAX_DYNAMIC_OFFSETS` (32) dynamic buffers. Rejected layouts and
  groups validate before allocating, and dispatch/draw usage scratch stays on
  the stack up to `RESOURCE_STACK_USES` uses, so ordinary failures never touch
  the allocator.
  Float32 textures require the explicit device-enabled `float32_filterable` policy
  for Float bindings; default admission is UnfilterableFloat regardless of driver
  capability. Features are copied for later resolve/submit revalidation. Normalized
  sampler descriptors are borrowed from the existing owner, not reconstructed.
- `usage_scope.c`: shared allocation-free dispatch/draw usage semantics,
  with effective dynamic ranges, whole-buffer read/write compatibility, texture
  mip/layer/aspect conflicts, external-plane reads and indirect input usage.
- `render_usage.c`: persistent whole-pass/bundle usage identities, including unused
  and overwritten setters, moved into immutable records through retirement.
- `commands.c`, `compute_pass.c`: sticky encoder admission, pass locking, transfer,
  direct/indirect compute snapshots and single-use submission self-leases.
  After a rejected begin, `doe_resource_*_pass_rejected` returns the WebGPU
  invalid pass: one encoder lease, no records, the encoder's rejection reported.
  A command on an ended pass returns STATE without failing its encoder; End
  checks the parent lock (`doe_resource_*_pass_closed`); a failed Finish may
  return `doe_resource_commands_rejected`, an invalid command buffer that submit
  and program claims reject.
  After a failed bundle finish, `doe_resource_render_bundle_rejected` returns
  an invalid bundle that has no draws and fails execution.
- `queries.c`, `external_textures.c`, `render.c`, `render_state.c`: real native query-pool ownership,
  retained imported plane views, and attachment/draw/query replay snapshots.
  Render begin/end packets bracket query events, including empty query scopes.
  Pass timestamps retain delayed end-write pools; indirect records retain arguments.
- `bundles.c`: reusable immutable draw snapshots sharing pass state/validation,
  with a 4096-operation budget and transactional storage move on finish. Replay
  creates the same retained command packets, not fake attachments or a renderer.

Uses existing `doe_instance_allocator` and optional `doe_label_store`. No second
label store, future queue, callback dispatcher or error-scope stack is present.
Frontend descriptor rejections use the same encoder sticky error owner. The
backend remains responsible for actual initialized storage, feature/hardware
limits, shader preconditions, synchronization and physical execution. Unknown
completion, cancellation and timeout never authorize dropping submission leases.

Atomic references support shared lifetime. Mutations, device-state admission and
borrowed views require caller serialization. A successful map attaches a borrowed
pointer to the START of its admitted span, after backend-established completion.
Mutable ranges require Write mode; const ranges accept Read or Write. Native
callbacks must not resurrect finalizing objects. The optional label owner must
remain open through final release.

Reusable-program preparation uses the command buffer's existing submission state.
`reserve_program` validates fresh/live dependencies and acquires one preparation
reference. While reserved, ordinary submission and competing program/replay claims
reject. A fallible backend constructor must finish native preparation privately,
then `publish_program` converts that lease to permanent program ownership without
allocation or another retain. On failure, `abort_program` restores freshness and
drops only the unpublished preparation lease. It remains usable for cleanup after
device loss; ordinary submission still validates the actual dependencies. Callers
serialize preparation and dependency mutation and finish each reservation once.
`claim_program` remains the immediate reserve/publish convenience.

Published program release never restores ordinary eligibility. It drops its
program lease exactly once, while an unresolved replay retains its separate
submission lease. Pending settlement preserves it; terminal settlement releases
it. A duplicate release or new replay after program release rejects without
dropping another reference. `make check-program-reservation` exercises constructor
allocation failure followed by ordinary fallback, exact reservation/publication/
replay references, unavailable-device abort and competing claims over immutable
dependencies. The normal `check` target includes this host control; it executes
no backend or GPU work.
The [transaction receipt](DEVELOPER_RECEIPT_PROGRAM_RESERVATION.json) binds the
exact owner inputs, full resource checks and UBSan checks; native constructor
integration and backend execution retain their own evidence.

## Source-led behavior and boundaries

Semantic sources include retained `native/resource/{doe_buffer_ops_native,
doe_texture_sampler_native,doe_texture_validation,doe_bind_group_native,
doe_query_native,doe_external_texture_native,doe_command_texture_native}.zig`,
`native/command/{doe_buffer_copy,doe_buffer_texture_copy,doe_command_recording,
doe_command_references}.zig`, `native/support/doe_native_object_types.zig`,
`native/vulkan/vulkan_render_attachments.zig`, backend Vulkan texture commands,
and `contracts/{binding,texture,texture_copy,texture_format_layout,resource_lease}.zig`.

Named vendored WebGPU enums are authoritative at this new seam. Retained Zig and
vendored headers disagree on RGBA16/RGBA32 numeric ordering: Zig RGBA16Uint is
0x24 while the header assigns that value to RGBA16Unorm. Native adapters must
translate semantic names explicitly, not cast legacy values. No retained ABI
was changed. Duplicate binding, same-device, range, read-only-map and mapped-submit
admission deliberately fail closed rather than reproducing silent legacy drops.
These stricter checks are not a claim of complete parity.

Bundle replay deliberately corrects retained Vulkan's binding restoration: logical
pipeline/group/vertex/index bindings clear even for an empty list, while dynamic
viewport/scissor/blend/stencil state survives. Each bundle starts self-contained.
This follows the [official executeBundles semantics](https://gpuweb.github.io/types/interfaces/GPURenderPassEncoder.html#executeBundles),
not the legacy internal save/restore helper. Host tests require rebinding and
exercise empty lists, independent snapshots, replay failure and pending leases.
Declared bundle depth/stencil read-only compatibility is checked even when the
bundle contains no draws; it is not inferred only from individual pipelines.

Dispatch validation follows [WebGPU usage scopes](https://gpuweb.github.io/gpuweb/#usage-scopes):
read/write incompatibility is whole-buffer, even for disjoint ranges. Multiple
writable-storage bindings visible to the same shader stage may share one buffer
only at disjoint effective ranges; readonly overlaps are permitted. Writable
alias checks preserve each entry's stage mask, allowing visibility0 or disjoint
stages without weakening unconditional whole-scope usage compatibility. Texture conflicts use mip/layer/aspect identity.
All groups and indirect input are admitted before offset allocation or command
publication. Empty pipeline-layout slots require no group and normalize to null
compute snapshots with zero offsets; ignored stale bindings are not submitted.
Explicit NULL slots follow the same required-group predicate. Constructor-time
normalization discards every zero-entry layout before retention/exclusivity;
automatic empty holes can be retrieved and reused in explicit layouts without
claiming a pipeline, while nonempty automatic layouts remain exclusive. The status-returning
pipeline-layout getter lazily owns one empty layout for absent/trailing indices
below MAX_GROUPS, avoiding eager allocation and frontend caches. Failure leaves
output and cached identity untouched; OOM can be retried under caller serialization.

Render scopes reuse these conflict rules across attachments, every group/vertex/
index setter (including overwritten or unused state), indirect input and bundle
replay, even empty bundles. Read-only depth/stencil aspects are admitted separately.
Repeated storage usage across draws is permitted; overlapping writable bindings
within one draw are not. Successful setters retain exact resource identities;
unbind and bundle state reset cannot remove historical uses. The finished scope
moves to the final pass record and remains alive through pending submission.
Destroyed, mapped and expired unused resources are checked before native submit.
A destroyed texture that is still referenced is admitted by view creation, bind
group creation, setBindGroup, copies and render attachments; submission rejects it
(WebGPU validates destroyed textures at submit) and queue writes reject it at once.
Every bind group a compute pass sets, whether or not a dispatch uses it, is held
by the encoder and checked at submission (WebGPU `[[used_bind_groups]]`).
No second native recorder, resource registry or completion engine is introduced.
Nullable vertex bindings clear only logical state after offset/size validation
against a zero-sized buffer; historical uses remain. Render/pipeline/bundle device,
layout and descriptor getters borrow existing objects without extra leases.
Occlusion indices are single-write within each pass, including empty query scopes;
a later pass may reuse them. A bounded per-pass bitmap enforces this before
command allocation, without introducing native query availability state.
Color/depth clear values are admitted only when the corresponding load operation
is Clear; Load and depth-read-only paths ignore them, including nonfinite values.
Native clear conversion still validates its actual scalar representation.

Vertex layout normalization preserves the pinned C ABI distinction: Undefined
step mode plus no attributes is a NULL hole, whereas explicit Vertex/Instance
with no attributes still requires a buffer. The canonical required-slot getter
serves native lowering. Direct ranges use first+count even when count is zero;
indexed per-vertex and indirect ranges are not guessed from unknown GPU indices.
The actual backend must enforce its robust access guarantees. Named primitive
and strip-index identities are retained in the pipeline and checked by the shared
pass/bundle/submit draw validator. Frontend preflight failures use explicit pass
and bundle invalidation entrypoints, preserving the canonical first error.

Query descriptors use one allocation-free preflight for named type and count
0..4096, following current [WebGPU query creation](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createqueryset)
rather than retained native's nonzero-only, unbounded count. A zero-query identity
retains its device but needs no native pool. Every write/resolve still requires an
index strictly below count; zero-count resolve at the end is invalid. Device
feature enablement and timestamp-unit normalization remain consumer obligations.

Copy admission shares only checked byte-footprint arithmetic with native Vulkan
through `../common/include/doe_copy_footprint.h`; format, pitch/default, zero extent,
alignment, capacity and native granularity policies remain in their actual owners.

Render ownership admits up to eight indexed color slots (a NULL view or Undefined
format is an unused slot) and optional depth/stencil, requiring at least one actual
attachment, with a multisample resolve destination per slot. `src/render_targets.c`
alone owns slot normalization (legacy single-color fields are slot 0 input only and
are never stored), the requested `max_color_attachments` and
`max_color_attachment_bytes_per_sample` limits (zero selects the WebGPU baselines 8
and 32), the WebGPU render-target byte-cost/alignment table and pass/pipeline/bundle
layout equality ignoring trailing unused slots. One texture subresource cannot occupy
two slots. `tests/test_color_targets.c` checks sparse layouts, limits, alignment,
aliasing and legacy/slot mixing on the host.
The eight-vertex-buffer/four-group envelope covers direct/indexed/indirect records,
timestamp writes and occlusion scopes.
The owner itself does not execute graphics/query work. The standalone native
consumer below exercises a narrower real graphics subset. Surface presentation, binding arrays and external-texture color conversion remain
unsupported. Actual map/future delivery and native shader execution are consumer
responsibilities. Unsupported consumer command kinds must reject before submit.

## Focused checks and evidence

`make -C runtime/c/resources check` builds GCC C11/Werror into `/tmp`: 13 base and
8 extended, 6 bundle, 8 binding-feature, 7 dispatch-usage, 10 render-usage,
1 query-admission, 3 draw-admission, 4 attachment-admission, 4 resolve-admission,
3 view-default, 6 debug-group, 3 depth-only and 5 binding-limit host suites cover transactional
failure/untouched outputs, first error,
label cleanup, atomic references, stale/read-only maps, copies/aliases, dynamic
snapshots, pass abandonment/unbinding, shader/query/external/render leases and
unresolved submission lifetime. Host native tokens test finalization, not GPU work.

The real native lifetime consumer uses the backend's canonical archive:

```
VK_ICD_FILENAMES=/usr/lib/chromium/vk_swiftshader_icd.json \
  timeout --signal=TERM --kill-after=5s 60s \
  make -C runtime/c/resources vulkan-lifetime
```

It explicitly permits software and refuses a non-software adapter. Actual
SwiftShader fill/buffer copy/full image upload/image copy/readback validates early
caller release and unresolved-work retention. Full writes precede image reads;
this is not a substitute for general WebGPU zero initialization. Only that native
transfer subset is established by that consumer, not queries or broader graphics.

The reusable-bundle consumer uses the same explicit ICD selection and timeout,
with target `vulkan-render-lifetime`. One self-contained bundle is replayed under
three dynamic scissors inside one real native pass: two visible draws produce
48 magenta and 16 blue pixels in an 8x8 target, and a clipped draw changes nothing.
The ordered draw/query span consumes BEGIN/END occlusion events without another
recorder or availability map. Actual hit, clipped, empty and unwritten results
resolve as one nonzero and three zeros. Drawing uses a real 4x RGBA8 attachment
with 4x D32S8 depth/stencil and a retained single-sample resolve target. A following
zero-draw pass reuses the hit index as an empty query, loads read-only depth/stencil,
resolves color before discarding its multisample source, and preserves the image.
A third actual depth-only pass discards depth while preserving stencil; it has no
fabricated color attachment and does not claim depth-only drawing.
All neighboring query-buffer bytes retain their sentinel values. Missing end and
unsupported interleaved timestamp events reject before native work, preserving
consumed output. All caller resources and the bundle are released before native
recording; pending native reads/releases reject, and all twelve adopted payloads
finalize only at terminal resource settlement. Native commands retain exact
physical objects through later readback and retirement. Shader fixtures are
retained Tint `particles.wgsl` vertex/fragment SPIR-V, not C graphics compiler
output; their hashes/provenance are in the backend `SMOKE_IMAGE_FIXTURES.json`.
The bounded consumer admits three direct draws, one RGBA8 target with optional
D32S8 and 4x resolve, one uniform binding, one vertex buffer and one occlusion pool.
Indexed/indirect and dynamic blend state reject; depth-only drawing is outside
this consumer. `DEVELOPER_RECEIPT_FULLPASS.json` binds the optimized and fully
instrumented software runs. Earlier sealed graphics receipts preserve their narrower
historical subsets. Occlusion values
establish only zero/nonzero visibility, not precise sample counts. This is
software execution and ownership evidence, not public native render ABI
integration, graphics parity or physical GPU qualification.

The `vulkan-query-lifetime` target uses the same explicit software ICD and timeout.
It consumes timed-pass begin/end and command timestamp records through the native
query availability/reset owner, resolves five packed raw device ticks with two
unwritten entries zeroed by native commands, and verifies all neighboring bytes.
No CPU-generated query values or second availability map are used. Public
256-byte resolve alignment remains stricter than the native packed8-byte seam;
CPU reads remain unavailable until actual native completion. Raw ticks are not a
portable WebGPU nanosecond result: the retained GPU normalization shader/calibration
path is separately implemented in the backend/native owners; this raw consumer
does not establish their public timestamp semantics.

```
ASAN_OPTIONS=detect_leaks=0 make -C runtime/c/resources \
  BUILD=/tmp/doe-c-resources-asan \
  CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread -fsanitize=address,undefined -fno-omit-frame-pointer' check
```

Address/undefined sanitizer checks pass; LeakSanitizer cannot run under this
ptrace environment. Exact fixture allocation/native-release counters reach zero,
not whole-program leak qualification. `DEVELOPER_RECEIPT_EXTENDED.json` binds the
software-transfer checkpoint to unchanged source hashes, including consumed backend
bytes. `DEVELOPER_RECEIPT_PASS_CONTROLS.json` binds the later host-only timed-pass,
indirect-render and shared-preflight follow-up; it does not extend GPU evidence.
`DEVELOPER_RECEIPT_BUNDLES.json` binds the host-only reusable-bundle extension.
`DEVELOPER_RECEIPT_RENDER.json` binds the later binding-feature correction and
real software bundle/render and transfer consumers, including backend and shader bytes.
`DEVELOPER_RECEIPT_QUERIES.json` binds the subsequent raw-timestamp consumer and
empty-bundle read-only correction; it does not claim nanosecond normalization.
`DEVELOPER_RECEIPT_USAGE.json` binds historical dispatch-only scope admission.
`DEVELOPER_RECEIPT_SCOPES.json` binds the subsequent shared arithmetic, empty-layout,
whole-render-scope and query-descriptor checks.
`DEVELOPER_RECEIPT_RENDER_STATE.json` binds later borrowed graphics accessors,
nullable vertex unbind and pass-local occlusion admission host checks.
`DEVELOPER_RECEIPT_NATIVE_PASS.json` binds the later actual single-pass span and
empty LOAD/STORE consumer; it does not qualify native occlusion execution.
`DEVELOPER_RECEIPT_LAYOUT_VISIBILITY.json` preserves the first NULL-slot and
visibility0 reproductions and binds their focused corrections.
`DEVELOPER_RECEIPT_OCCLUSION.json` binds the subsequent real ordered occlusion
span, mixed results, later-pass reuse and exact resource/native retirement.
`DEVELOPER_RECEIPT_FORMAT_CLASS.json` binds the shared checked format/aspect
classification, channel counts, unchanged-output/feature-independence controls
and the first ignored-clear reproduction with its correction.
`DEVELOPER_RECEIPT_DRAW_ADMISSION.json` binds canonical vertex/index admission,
frontend rejection, automatic-empty reuse and their preserved first reproductions.
`DEVELOPER_RECEIPT.json` preserves the earlier smaller historical check.
Run the common source-size check; every owned C source/header is below 999 lines.

Attachment descriptor admission is shared by pass begin and immutable record
submission. Missing or read-only depth/stencil aspects require undefined operations;
transient attachments require Clear/Discard for each present aspect. Transient
creation fixes usage to TransientAttachment|RenderAttachment, one 2D mip/layer
and no view-format list; its views cannot drop the transient usage bit. One-dimensional
textures have one mip and cannot be render attachments, and multisample textures
require attachment usage and reject storage (copy usage is admitted: texture
copies need equal sample counts and the whole subresource, buffer copies one
sample). These structural rules follow the current
[texture descriptor](https://gpuweb.github.io/gpuweb/#validating-gputexturedescriptor)
and [attachment](https://gpuweb.github.io/gpuweb/#depth-stencil-attachments) contracts,
correcting retained validation omissions rather than importing unsupported native
features. Feature enablement, format capabilities, physical memoryless allocation
and backend attachment contents remain separate owners. The retained Vulkan path
explicitly rejects transient usage; these host checks do not claim otherwise.
`DEVELOPER_RECEIPT_ATTACHMENT_ADMISSION.json` preserves the four first rejected-input
reproductions and binds the corrected descriptor/lifetime checks.

Optional multisample resolve destinations now use the same retained pass targets,
immutable packets and whole-pass usage scope. Structural admission checks exact
texture/view formats, selected mip extents, source/destination sample counts,
same device, availability, attachment usage and a non-transient destination.
Transient multisample sources can resolve into persistent targets. Resolve identity
survives empty passes, logical binding resets and repeated bundle replay; unused
sampling setters and empty-bundle usage histories still conflict with the destination.
Four begin-allocation failures, destroyed-before-submit and pending/failed retirement
preserve existing ownership contracts. Backend format/resolve capabilities remain
separate admission. `DEVELOPER_RECEIPT_RESOLVE_ADMISSION.json` binds these host
checks; the later full-pass consumer above separately establishes actual bounded
software multisample execution.

Texture-view defaults are dimension- and aspect-specific, not uniformly the rest
of the parent texture. Explicit 2D views default to one layer, Cube to six, and
array views to the remaining layers. Combined depth/stencil aspect views expose
the effective Depth24Plus/Depth32Float or Stencil8 format through the same checked
format owner used by binding sample classification. Usage identity still comes
from the retained parent texture and selected aspect. Render attachments must
select all actual parent aspects, which permits DepthOnly on depth-only parents
and StencilOnly on Stencil8, but not partial views of combined parents. The compact
resource API still accepts zero or UINT32_MAX as default counts; the pinned public
ABI adapter must distinguish invalid explicit zero from its UINT32_MAX sentinel.
These correct retained omissions against the
[view default rules](https://gpuweb.github.io/gpuweb/#resolving-gputextureviewdescriptor-defaults).
`DEVELOPER_RECEIPT_VIEW_DEFAULTS.json` preserves first failures and source-bound
host checks. Native aspect/reinterpretation support remains separately explicit.

Debug commands now validate the same encoder, compute-pass, render-pass and bundle
owners rather than succeeding unconditionally. Each has independent checked nesting;
pop underflow, overflow, inactive/locked use and unbalanced end/finish preserve the
canonical sticky error. Parent groups can span a pass, but child groups cannot pop
parent state. Empty bundle execution clears bindings, not the pass debug stack.
Markers and nesting add no resource leases, allocations or synthetic GPU records;
optional diagnostic names remain with the existing label/native diagnostic boundary,
not a second string store. This intentionally repairs retained no-op debug validation
against the [debug command contract](https://gpuweb.github.io/gpuweb/#debug-markers).
Five focused suites cover all four owners, balance/error isolation, untouched failing
arithmetic and attachment retirement. `DEVELOPER_RECEIPT_DEBUG_GROUPS.json` binds these
host checks; capture-tool labels and frontend exports are separately owned.

Depth/stencil-only passes, pipelines and bundles now share the same canonical
attachment-layout admission, with no fabricated color resource. Pass extents and
sample identity come from the actual retained attachment, absent color values are
ignored, and resolve without a color source rejects. Eighteen depth/stencil-format,
sample and read-only cases exercise empty boundaries and retirement. Bundle replay,
layout/read-only mismatches, four begin-allocation failures and destroyed-before-submit
controls reuse the existing packet/scope owners. Both attachment formats absent still
reject before allocation or native adoption. These follow the
[pass attachment requirement](https://gpuweb.github.io/gpuweb/#dom-gpurenderpassdescriptor)
and [bundle layout requirement](https://gpuweb.github.io/gpuweb/#dom-gpurenderbundleencoderdescriptor).
`DEVELOPER_RECEIPT_DEPTH_ONLY.json` preserves first failures and host evidence;
actual depth-only drawing remains a separate backend/consumer admission.

ABI debug-label rejection uses the explicit compute-pass invalidator, matching the
other recording owners. Non-rejection statuses never mutate state; real rejection
preserves first error and inactive/device-loss admission. This closes the typed
frontend boundary without fake debug operations or frontend counters.
`DEVELOPER_RECEIPT_COMPUTE_INVALIDATE.json` binds the focused controls.

The optional `vulkan-indirect-lifetime` consumer now compiles a WGSL argument
producer, backend-generated dimension guard and atomic counter through the existing
shader artifact owner. GPU-produced records exercise requested limits `{2,3,4}`,
valid/over-limit X/Y/Z/zero/valid dispatches and private scratch reuse. The exact
counter is four; original argument records and padding remain unchanged. Resource
snapshots own caller-released arguments, bindings and pipelines through terminal
settlement, while native commands independently retain validator scratch/pipelines.
Public Indirect usage is unchanged: COPY_SRC is an internal backing capability,
not a new caller requirement. No GPU argument value is inspected on the CPU before
execution, and no resource-side validator or queue is added.

Set `COMPILER_LIBRARY` explicitly; `SHADER_ROOT` can select a coherent immutable
artifact-owner snapshot during parallel ABI work, with `SHADER_BUILD` keeping its
build output separate. `DEVELOPER_RECEIPT_INDIRECT.json` identifies the exact ABI9
producer and committed artifact-owner sources, not the concurrently authored text
API. This bounded software consumer is not a public indirect ABI or physical-GPU
qualification. Product-sensitive workgroup/global-invocation-index builtins remain
compiler-unsupported in this producer; later admission requires coordinated guard
rules. Indexed/indirect rendering is outside this execution witness.

Initial mapped-at-creation attachment now consumes the existing generation token,
including a zero-byte mapping whose borrowed pointer is NULL. Pointer nullness is
not an ownership state. Duplicate/stale successful or failed completions reject;
invalid nonempty NULL completion leaves the token available for a valid retry.
Empty end-of-buffer mappings and the last UINT64_MAX token preserve checked range,
untouched-output and retirement contracts. `DEVELOPER_RECEIPT_MAP_TOKEN.json`
preserves the first failure. This was a private resource-owner contract bug;
current public native zero-size backing is non-NULL, so public exposure is not claimed.

The optional `vulkan-indirect-render-lifetime` consumer uses the same explicit
compiler/snapshot options. A C-compiled GPU producer supplies padded draw records;
the backend's C-compiled guard prepares distinct scratch for eight draws in one
actual native pass, including bundle replay. Retained Tint graphics shaders render
48 magenta and 16 blue pixels. Per-draw occlusion distinguishes valid draws from
vertex-index overflow, instance overrun, nonzero firstInstance, zero-count and
one-byte-short subrange rejection. True unused vertex holes and stride-zero
instance input remain valid controls. Original arguments/padding are unchanged,
and twelve native payloads retire after pending completion. Shared fixture helpers
replace duplicate artifact setup and retained SPIR-V loading; no production packet,
recording engine or resource-side GPU count validator was introduced.
`DEVELOPER_RECEIPT_INDIRECT_RENDER.json` binds the optimized/instrumented software
witness and the earlier compute/full-pass consumer regressions. It does not claim
C graphics compilation, indexed-indirect support, public native draw-indirect
integration, precise occlusion sample counts or physical qualification.

Viewport admission now uses the creating device's copied requested
`max_texture_dimension_2d`, not attachment extents. Internal standalone callers
may leave it zero for the baseline 8192; public adapters must provide their actual
requested limit. Negative origins, out-of-attachment viewports and zero extents are
valid within the [WebGPU viewport bounds](https://gpuweb.github.io/gpuweb/#dom-gpurenderpassencoder-setviewport);
scissors remain attachment-bounded. The upper-edge comparison avoids both integer
overflow and rounded acceptance of even a subnormal excess. Failed admission does
not replace raster state, preserves the first encoder error and allocates nothing.
This intentionally corrects the old C nonnegative-origin rule and missing limits,
while retained Zig's setter only stores values. `DEVELOPER_RECEIPT_VIEWPORT.json`
preserves those first failures and host boundary/lifetime controls. Native physical
limits and empty-raster lowering remain backend responsibilities; host acceptance
alone does not prove native rendering or physical qualification.

Pass draw budgets distinguish omission from an explicit zero through
`has_max_draw_count`. Omission ignores the value and selects 50,000,000; begin
normalizes the copied target once. Direct, indexed and indirect commands each
consume one budget entry even when their counts are zero, while boundaries,
markers and empty bundles consume none. Bundle lists preflight their total before
emitting any draw. Allocation failure leaves the count unchanged, and the final
UINT64_MAX count cannot wrap. This corrects retained Zig's explicit-zero-as-default
behavior to the pinned typed-chain initializer and
[WebGPU draw budget](https://gpuweb.github.io/gpuweb/#dom-gpurenderpassdescriptor-maxdrawcount).
`DEVELOPER_RECEIPT_DRAW_BUDGET.json` preserves the first failure and focused host
ownership checks; native descriptor-chain parsing and real execution are separate.

Pipeline attachment/read-only compatibility is checked when set, not postponed
until a draw that might never occur. Pass and bundle setters reuse the same
signature validator as draw/replay; rejection retains the old binding, acquires
no candidate lease and preserves the sticky error. Canonical layout construction
also rejects Vertex-visible writable storage buffers/textures and multisampled
Float bindings before allocation. Legal read-only Vertex, writable Fragment/Compute
and unfilterable/depth multisample layouts remain admitted. These follow the
[setPipeline rules](https://gpuweb.github.io/gpuweb/#dom-gpurendercommandsmixin-setpipeline)
and [layout-entry rules](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbindgrouplayout).
Existing visibility-alias controls use legal Fragment/Compute pairs; whole-pass
usage history remains independent of visibility. `DEVELOPER_RECEIPT_PIPELINE_ADMISSION.json`
preserves the four first failures and focused host evidence, not GPU qualification.
