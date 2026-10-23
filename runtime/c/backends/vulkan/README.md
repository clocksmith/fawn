# Experimental C Vulkan backend

This private C11 owner implements real Linux LP64 Vulkan 1.2 discovery, device and
queue selection, capabilities, host-coherent and device-local buffers, sparse
multi-group buffer/image/sampler descriptors, SPIR-V compute pipelines, copy/fill and
direct dispatch recording, asynchronous fence submission, readback and retirement.
Single-color offscreen rendering shares those allocation and completion owners.
The texture owner adds uncompressed color images, retained views/samplers and
mip/layer-aware padded transfers across 1D/2D/3D images.
It is not wired into the production provider. It is not full Vulkan/WebGPU parity.

Adapter selection has four explicit modes: the existing exact ordinal (the zero
default), first CPU device when software is explicitly requested and permitted,
first non-CPU device for a hardware request, or one exact device/driver UUID-pair
match. Native device creation uses the
adapter's copied UUID identity, so changing enumeration order or instance-local
handles cannot choose another device. Missing or duplicate identity matches
reject; no ordinal or hardware substitute is attempted. UUID properties come
from the core1.1 `VkPhysicalDeviceIDProperties` chain on the already-required
properties query. The [official identity contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceIDProperties.html)
distinguishes device identity from pipeline-cache compatibility.

`make check-adapter-identity` exercises the actual selector with typed native
enumeration/property procedures: changed handles/order, identical weak labels,
driver mismatch, duplicate/missing identity, GPU-before-CPU fallback and failed
enumeration. Native `vulkan-identity` additionally checks the adapter/device
bridge and unchanged output/cleanup after mismatch. These are host contract
controls. `vulkan-identity --software-context` also creates real CPU adapter/device
contexts when a software ICD is explicitly selected with process-local
`VK_DRIVER_FILES`; it submits no commands. `sanitize-adapter-identity` runs the
selector control under address/undefined sanitizers. No physical GPU identity or
device-execution qualification follows.

The retained consumer path is `native/vulkan/vulkan_compute_native.zig` through
`backend/dropin_resource_ops.zig` to the Vulkan device/resource/descriptor/pipeline/
command/synchronization owners. This boundary accepts compiler-produced SPIR-V;
it never recognizes shader source patterns or executes a CPU shader interpreter.

## Ownership and unsupported work

Read `include/doe_vk.h`. A context and all its objects require caller serialization.
Resources retain their creating context. Recording retains exact buffers and
pipelines, image views and samplers; descriptor pools belong to each dispatch until
command retirement.
Host accesses are blocked while commands may use a buffer. Successful fence
completion unlocks host access. Pending/uncertain command release returns `BUSY`
and does not consume the handle. A timed-out waiter must retain and settle it.
An uncertain submit can be recovered by explicit, potentially blocking queue-idle
observation. Its original failure never becomes successful output. Device loss
permits terminal cleanup and rejects readback. Other terminal submission failures
poison participating buffer contents: reads and recording reject until an explicit
full host rewrite, or replacement for device-local memory. Partial writes never
turn uncertain output into successful output. Caller references may be released
before submission; recorded ownership remains sufficient.

Device-local allocations are not initialized by this backend primitive. The
native WebGPU owner must record initialization before exposing reads. Host
allocations are initialized. Robust buffer access is required/enabled. Core baseline shader features are enabled, with the explicit optional demote
selection described below; unsupported feature use must be rejected by
the compiler/native admission path before creating a pipeline. This is not a
complete public validation boundary for untrusted SPIR-V.

Prepared-program caches and tuned hazard elision are not yet migrated here.
Indirect compute uses the explicit GPU validator below; unchecked forwarding is not exposed.
The retained production owners remain authoritative for those behaviors.

## Texture transactions

`include/doe_vk_texture.h` uses named native format values, never WebGPU numeric
casts. Eighteen uncompressed color formats plus four attachment-only depth/stencil formats
are admitted when native format support exists. Compressed formats, multisampling,
reinterpretation, swizzles and cube-array
feature enablement remain outside this increment. Native views retain their image.
A texture is held by one unresolved command owner to avoid stale recorded layouts.
Layouts and per-mip/layer initialization are speculative until successful native
completion; discarded/failed recordings cannot publish them. Undefined subresources
require a full upload before partial updates or reads. Terminal submission failure
poisons the image instead of guessing a layout or exposing uncertain output.
`doe_vk_texture_format_caps` exposes named optimal-tiling capability facts; image
creation checks every requested usage bit. These observations do not enable shader
features or admit every dimension/extent combination. The exact format flag values
are from the [Khronos reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkFormatFeatureFlagBits.html), recorded in ABI provenance.

`make native-texture NATIVE_ARGS=--allow-software` with an explicitly selected
existing software ICD checks real image/view/sampler creation, two mip levels and
two array layers, padding/last-byte layout, image copy/readback, initialization
rejection, competing ownership rejection and early caller release. It is not a
physical run or a complete texture-feature qualification.

## Typed shader resources

`include/doe_vk_bindings.h` records mixed uniform/storage buffers, sampled images,
storage images and samplers against the pipeline's immutable sparse layout. The
existing buffer-only dispatch API delegates to the same descriptor owner. Tags,
native usage, ranges, initialization and context identity are checked. Compiler
and native admission must still check shader sample type, view dimension, format,
access and filtering compatibility; this primitive does not infer WGSL meaning.
Image views and samplers stay retained through command retirement. Image layout
transitions and transfer/shader visibility are real native barriers.

`make native-descriptors SPIRV=<retained import_level fixture>` uses the fixture
bound in `SMOKE_IMAGE_FIXTURES.json`. It exercises a real sampled-image shader,
not the still-separate C texture compiler: 64 exact fetched alpha values, sparse
mixed descriptors, full upload and early caller release. An unused sampler binding
checks native descriptor lifetime, not sampling semantics. `make native-storage SPIRV=<retained export_level fixture>` exercises an actual
storage-image shader with 64 exact means and 256 exact RGBA8 bytes after shader-to-
transfer barriers. Both fixtures are pinned; neither is C texture compiler evidence.

## Offscreen graphics

`include/doe_vk_render.h` consumes a borrowed immutable span of direct instanced
raster draws as one actual single-color native render pass. The optional clear
executes once at begin; otherwise begin loads initialized contents. End always
stores. An empty span still executes the real load/clear/store pass. The old
clear-only and single-draw conveniences delegate to this same recorder.

Whole-span structural validation precedes resource preparation. All descriptor
admission, allocations, retained leases, barriers and image transitions finish
before native begin. The in-pass loop only binds prepared state and issues draws;
it never inserts an invalid global barrier inside a render pass. A later invalid
draw or descriptor therefore cannot leave a native pass open. Any failure poisons
the command owner, which must be discarded instead of partially submitted.

Exact views, one compatible render pass/framebuffer, descriptors and vertex
buffers remain retained by the existing command lifetime until retirement. A
clear initializes only the selected full 2D mip/layer; LOAD cannot read undefined
contents. Resource pass/bundle packets retain their canonical ordering upstream;
this borrowed span is not another resource recorder or reference-count owner.

Pipeline state admits format-supported samples, one color target, eight vertex layouts/sixteen
32-bit scalar/vector attributes, native blend/cull/topology controls and dynamic
viewport/scissor. Blend constants default to pipeline state; explicit per-draw overrides use real
native dynamic state after whole-span finite-value validation. The full descriptor
adds one depth/stencil attachment, independent per-aspect load/clear/store/discard,
read-only admission and dynamic stencil reference. Multiple color targets
remain unsupported. Indexed and guarded indirect draws are described below.
Unsupported resource packets must be rejected, not silently dropped. Graphics
requires `require_graphics_queue=true` at context creation when the device offers
separate compute-only queues; default compute queue selection is unchanged.

`make native-render VERTEX_SPIRV=... FRAGMENT_SPIRV=...` uses the two retained Tint
fixtures bound in `SMOKE_IMAGE_FIXTURES.json`. Blue clear plus two preserving draws
with vertex attributes, uniform data and disjoint scissors produces 64 exact RGBA8
pixels. It releases caller references before submission. This retained-fixture test is real Vulkan graphics, not C graphics compiler
evidence or physical qualification; separate source-compiler consumers own that proof.

## Timestamp queries

`include/doe_vk_queries.h` owns real timestamp pools with retained native reset,
TOP/BOTTOM write and packed64-bit resolve commands. Queue timestamp width and
period are queried, not assumed; public feature admission remains upstream. Each
entry must be reset before a write. One unresolved command owns each pool; its
availability state publishes only after successful native completion. Terminal
failure poisons the pool rather than guessing results. A new pool replaces it.

Resolve splits written runs into native WAIT/64-bit copies and never-written or
reset runs into native zero fills, avoiding an unissued-query wait. Zero-count
resolve is a validated no-op. The resource owner retains its WebGPU256-byte offset
rule while this native primitive requires8-byte alignment. `make native-queries`
checks real writes/resolves, unwritten zero semantics, fill payload visibility and
early caller release with a5-second fence deadline. Tick values are not benchmark
results or physical qualification.

`doe_vk_occlusion_pool_create` uses the same query state/lease/resolve owner,
requiring a graphics-capable queue but no timestamp feature. Supply that pool to
`doe_vk_commands_render_pass_ops` with typed DRAW/BEGIN_OCCLUSION/END_OCCLUSION
operations. The complete span rejects nesting, an unmatched end, an unclosed
query, out-of-range or duplicate indices before native begin. Each actual written
slot is reset outside the pass; native query events then bracket the real draws.
Later passes may reuse a slot. Empty queries execute native begin/end and return
zero; they are not synthesized CPU values. Unwritten slots still use native zero
fills, never an unissued WAIT. Caller release before submit is safe through the
existing command owner. Timestamp-write and nanosecond-normalization operations
reject an occlusion pool instead of reinterpreting it.

Occlusion uses flags zero, without the optional precise-count feature: only zero
versus nonzero is guaranteed, as required by the public occlusion contract. This
follows the [Vulkan query definition](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBeginQuery.html),
checked 2026-10-23. The pool enum value is separately bound to the official
[query type reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkQueryType.html).
The retained pinned declarations already define native begin/end query signatures.
The render smoke preserves its exact pixel oracle while checking visible, empty,
clipped, unwritten and later-pass reused query slots, with early pool release.

## ABI provenance and checks

No SDK headers were available. The selected declarations are a narrow translation
of the existing source hashes in `ABI_PROVENANCE.json`, not invented opaque-sized
struct storage. `tools/check_abi.py` refuses source drift and independently models
the pinned extern fields to compile 1344 size/offset/alignment assertions over 76
records, including retained unions, plus ten format/query/dynamic-state constant assertions verified against the linked Khronos
references. No SDK or other dependency was downloaded. It does not prove the retained declarations against an official SDK.
`VK_ERROR_UNKNOWN` retains the existing Zig `UnsupportedFeature` classification;
raw native error codes remain available. No Zig runs or links in the C backend.

From this directory:

```
make all check
make sanitize
make probe
```

Outputs default to `/tmp/doe-c-vulkan`. The sanitizer recipe disables only leak
scanning because this hosted environment uses ptrace, which LSan rejects. ASan
and UBSan remain enabled. Explicit acquisition/release assertions still execute.
Run leak scanning separately in a supported environment before qualification.

For an actual native compute check, compile `tests/compute.wgsl` using the selected
C compiler, then run `make native SPIRV=/tmp/compute.spv`. No compiler invocation
or dependency fallback is hidden in the backend build. If intentionally exercising
an existing software ICD, set `VK_ICD_FILENAMES` for that process only and pass
`NATIVE_ARGS=--allow-software`. Default discovery rejects CPU adapters. Software
execution proves native API consumption, not physical GPU support or performance.

## Integration proposal

Keep selection default-off. A C native resource wrapper may adopt a successful
`doe_vk_buffer *` and finalize it with `doe_vk_buffer_release`; its device lease
retains/releases `doe_vk_context`. Recorded commands must retain actual backend
objects independently of public wrapper lifetimes. No new shared object registry
or replacement public reference-count authority is introduced. Shared build and
native consumer edits belong to the integration lead, not this backend directory.

### Explicit viewport convention

`doe_vk_render_draw.viewport_convention` defaults to `DOE_VK_VIEWPORT_RAW_VULKAN`,
preserving the retained positive-height Vulkan behavior and existing Tint fixtures.
For unmodified WebGPU vertex Position outputs, select `DOE_VK_VIEWPORT_WEBGPU`:
the one viewport owner emits native `y + height` and `-height`, with unchanged
x/depth/scissor. Source compilation must not additionally negate Position.y.
The pipeline's `clockwise_front` remains explicit native framebuffer winding,
not an implicit second coordinate correction. Old private-ABI consumers rebuild
with the appended field; this does not change the pinned public WebGPU ABI.

This follows [Vulkan's negative-height viewport definition](https://docs.vulkan.org/refpages/latest/refpages/source/VkViewport.html)
and its [framebuffer-space front-face rule](https://docs.vulkan.org/refpages/latest/refpages/source/VkFrontFace.html),
checked 2026-10-23. Negative height requires Vulkan 1.1, already the loader baseline;
the helper still rejects older contexts. Host tests check offset and full-size
transforms, raw preservation, invalid enum/version and failure output preservation.
A full-screen triangle is not orientation evidence; the shader owner's asymmetric
C-source consumer separately checks actual target orientation.


### In-command timestamp normalization

`doe_vk_timestamp.h` composes the existing raw query owner with the retained exact
GPU nanosecond conversion. Generate WGSL using the actual context's positive
binary32 period and valid counter bits; compile through the independent shader
owner; pass those same calibration values, workgroup size and validated SPIR-V to
the normalizer constructor. The constructor checks calibration and scratch/dispatch
limits before native acquisition. The retained default workgroup size is 64 from
`config/vulkan-timestamp-policy.json`; this is an explicit caller parameter, not a
second configuration reader. `TIMESTAMP_PROVENANCE.json` binds that policy and the
retained source; `tools/check_timestamp_source.py` verifies the embedded WGSL body
byte-for-byte and refuses source drift.

Nanosecond resolve records query copy/zero into device-local scratch, conversion
compute, and destination copy in the same command stream. Subsequent copies see
nanoseconds; CPU completion callbacks never patch output. Conversion masks unused
counter bits and computes the exact integer floor of the binary32-period product,
returning its low 64 bits, matching the retained numerical policy. It uses six
base-2^16 limbs and does not require shaderInt64. Unwritten entries remain zero.

The normalizer owns scratch and pipeline, but recorded commands independently
retain those existing owners. Caller release before submission is safe; unknown
execution still blocks command release. Scratch may be reused on this serialized
single queue because every resolve contains ordered barriers and a complete copy.
Terminal failed work poisons scratch, so construct a replacement normalizer rather
than reuse failed output. A shorter resolve binds only its exact count*8 range.

The backend archive has no compiler dependency. Only optional `native-timestamp`
links explicit `SHADER_LIBRARY` and `COMPILER_LIBRARY` inputs. That consumer tests
seeded fractional/masked/subnormal/overflow/identity arithmetic separately from
actual timestamp queries, same-batch copy, short scratch reuse and early release.
Seeded arithmetic is not timestamp evidence; query values come only from native
Vulkan commands. Use the same process-local software selector and bounded process
and fence waits as the other native checks. Public WebGPU feature exposure must
wait for the real compiler/normalization integration, not merely the raw tick API.

### Direct matrix compiler integration check

`make native-matrix COMPILER_LIBRARY=<held ABI8 library>` compiles the small
`tests/matrix_tail.wgsl` through the explicit ABI8 stage interface, validates
matrix dimensions/stride and exact24-byte minimum uniform reflection, then executes
five invocations using an exact24-byte uniform and140-byte storage descriptor.
It does not route matrix reflection through an older shader ABI or enable Vulkan
uniform-buffer-standard-layout. The exact35-float readback covers the final8-byte
tail, signed/dynamic column and row bounds, whole-matrix reconstruction, transpose,
vector products and determinant. Prefix/suffix sentinels and early caller releases
remain checked. The optional compiler link belongs only to this test executable,
not the native backend archive. Use an explicit existing software selector for
software evidence; this is neither physical nor complete matrix qualification.

### Graphics limits and dynamic blend constants

Capabilities expose actual driver vertex binding/attribute counts, stride/offset
limits and vertex-output/fragment-input component limits. Public admission still
intersects those facts with this owner's eight-layout/sixteen-attribute bounds;
reporting a native limit does not enable an optional shader feature.

`doe_vk_render_draw.override_blend_constant` selects its finite RGBA snapshot.
Otherwise the immutable pipeline default remains authoritative for older callers.
The one recorder sets the selected value before every draw, including transitions
between overridden and default state. No new command/state lifetime owner exists.
This core1.0 command needs no optional device feature. Its exact two-argument
signature and dynamic-state enum come from the linked official Khronos references
in ABI_PROVENANCE.json, separately from the retained Zig declaration list. The
ABI checker also compiles an ordinary compatible function-pointer assignment.
No SDK was downloaded; struct-layout evidence remains the pinned retained model.
The native render check preserves its older pixel/occlusion oracles and adds a
second exact64-pixel left-red/right-blue result using distinct per-draw constants.

A zero NULL vertex binding is admitted only for a pipeline slot without any
attributes. Populated slots retain checked ranges and command leases. Native
binds cover contiguous populated runs at their original indices, never passing a
NULL VkBuffer handle. The real render test leaves slot0 empty and uses slot1.
This is native no-fetch admission only: the public/resource owner still distinguishes
an Undefined-step-mode layout hole from an explicit Vertex/Instance slot that
requires a bound public buffer even when it declares no attributes.

Optional demote feature selection is explicit: set
`enable_shader_demote_if_supported` before device creation. The default remains
off. `caps.shader_demote_to_helper_invocation` reports successful enablement,
not merely API version or advertised availability. An absent extension/feature
keeps this optional feature disabled; native query/create failures are errors.
Compiler/native admission must reject SPIR-V capability 5379 unless enabled.
The private extension feature record is verified separately against the official
Khronos declaration in `ABI_PROVENANCE.json`, not attributed to retained Zig.
`make native-features NATIVE_ARGS=--allow-software` with the explicit software ICD
checks baseline/opt-in device creation only, not shader or physical qualification.

Depth/stencil uses the same full pass recorder and completion-owned image state.
`doe_vk_commands_render_pass_full` accepts the explicit attachment descriptor; old
color-only calls construct that descriptor and delegate. Initialization bytes are
per-aspect masks, so discarding depth does not discard stencil and an abandoned
clear does not publish either. A read-only aspect cannot clear/discard or use a
pipeline that may write it. LOAD requires prior initialization, including empty
passes. Views and native framebuffers stay retained through unresolved work.
Depth/stencil images admit 2D attachment, copy and/or sampled usage (cube
compatibility only when sampled); storage remains unsupported. Views carry an
explicit aspect (`doe_vk_texture_view_desc.aspect`), resolved by the same owner
as transfers: a sampled view of a combined format must select DEPTH or STENCIL
(descriptor admission rejects both), attachment views must cover every aspect,
and sampled reads require the view's aspects to be initialized. Sampled depth
uses the existing GENERAL descriptor layout. Transfers carry an explicit aspect on
`doe_vk_texture_region`: ALL for color and for all-aspect texture-to-texture
copies, DEPTH or STENCIL for buffer copies, which need exactly one aspect. Linear
bytes are per aspect (D16 depth 2, D32 and D32S8 depth 4, stencil 1); packed D24
depth has no linear layout here and buffer copies of it return UNSUPPORTED. The
native aspect mask reaches `VkBufferImageCopy`/`VkImageCopy`; layout transitions
still cover every aspect of the image. Reads require the copied aspects to be
initialized and full writes initialize only those aspects, using the same
per-aspect masks as attachments. Public per-format copy direction and full-
subresource rules belong to the resource owner. D16_UNORM is admitted as a named
retained format (value 124 from `vk_formats.zig`). `tests/depth_copy_contract.c`
checks these packets, creation rejection, per-aspect publication and partial-write
refusal with mocked native calls, not GPU execution.

The extended native render witness uses D32S8 with near/far depth tests, stencil
replace/equal, dynamic references, an empty read-only LOAD, independent rejected
fragments and an exact 64-pixel color readback. SwiftShader rejected the first
D24S8 fixture as unsupported; the retained failure was followed by explicitly
selecting D32S8, not a hidden backend fallback. `depth_contract` checks native
aspect/load/store records, discard, preflight errors, leases and rollback.

`doe_vk_commands_initialize_attachment` owns zero initialization through actual
empty native passes, not CPU availability mirrors. It examines the existing
speculative aspect masks, clears only undefined aspects of the selected 2D
image mip/layer span, and preserves valid aspects. Array/cube selections use
retained single-subresource views. Initialized selections are no-op. Use this
explicit primitive before public attachment publication and before sampling,
copying or LOAD of discarded attachments. Raw LOAD still rejects undefined data.
The full descriptor also admits depth/stencil-only passes and draws,
without manufacturing a color attachment.
The native witness verifies depth-zero after discard without losing stencil,
and exact zero color bytes after discard.

Multisample attachments use the same pass and image lifetime owners, not a second
resolve recorder. Texture/pipeline `sample_count=0` preserves the sample1 default.
Format capabilities report the actual 2D attachment-only sample mask, intersected
with framebuffer aspect limits. Every image also checks its exact format/type/
usage/flags combination, dimensions, mips, layers and samples through the native
image-format query before creation. The added core ABI record/signature has
separate official provenance and layout checks; this is not a driver test.

Multisample images currently require 2D, one mip/layer, no cube flag and exclusively
color or depth/stencil attachment usage. Copying, sampling and storage remain
unsupported for them. Pipeline sample masks and alpha-to-coverage use native
multisample state without enabling sample-rate shading or alpha-to-one. Public
WebGPU sample-count and shader-output admission remains upstream.

`resolve_target` is an optional same-format/extent single-sample color view.
The entire pass validates it and retains it before native begin. Native subpass
resolve occurs before the source store/discard, so discarding the multisample
source does not invalidate the resolved destination. Initialization remains
speculative until successful completion; unknown work retains both images and
all views. An empty LOAD pass can resolve, and the canonical initializer can GPU
clear undefined multisample attachments after discard. The pipeline's pass has
no resolve attachment, using Vulkan's explicit single-subpass compatibility rule
that ignores resolve references: https://docs.vulkan.org/spec/latest/chapters/renderpass.html.

The existing native render witness now exercises 4x RGBA8/D32S8, a sample1 resolve,
empty LOAD resolve, zero sample mask, alpha-zero coverage, resolve-before-discard
and zero-after-discard, with five exact 64-pixel oracles and early caller release.
This is explicit SwiftShader execution only, not physical or compiler qualification.

A depth/stencil-only pipeline sets `color_format=0`, an actual depth/stencil format,
zero color write mask, disabled blend and disabled alpha-to-coverage. Its fragment
shader may be entirely zero/absent, producing a real vertex-only native pipeline,
not a dummy fragment shader or rasterizer-discard substitute. A supplied fragment
shader is still permitted and validated. Color pipelines require a real fragment
shader. The Vulkan omitted-fragment-stage rule is explicit in
https://docs.vulkan.org/spec/latest/chapters/pipelines.html.

The same full-pass draw preflight selects the real depth/stencil target for
viewport, sample compatibility and aliasing checks; existing descriptors, vertex
leases, read-only admission and retirement are unchanged. The extended retained
native witness observes a vertex-only depth write using subsequent failing far
and passing near color draws, with an exact 64-pixel blue/magenta oracle and early
pipeline release. Host controls inspect native stage/attachment records and
allocation failure cleanup; they are not shader execution evidence.

GPU-produced indirect compute counts use `include/doe_vk_indirect.h`. The validator
owns one private 12-byte device-local storage/indirect scratch buffer and an actual
compiler-produced guard pipeline. Generate WGSL for the selected nonzero per-axis
limits, compile it through the existing compiler owner, and construct the validator
with those same limits. Admission requires limits no greater than the actual device;
public requested-limit policy stays upstream. The old unused always-unsupported
raw entry was replaced with this typed validator/mixed-binding boundary.

Every indirect call records exact argument copy, one GPU guard invocation, and
native indirect dispatch from scratch. Any over-limit axis zeros all three counts;
zero dimensions remain no-ops. The same buffer/descriptor/command owners supply
visibility, exact leases and poisoning. Original arguments are not modified by
the guard; there is no CPU readback or argument surrogate. Native argument buffers
need INDIRECT|COPY_SRC, while public usage still records only what was requested.
Commands retain the private scratch and both pipelines independently, including
after caller validator release. Replace a validator whose scratch was poisoned by
a terminal failed submission; unknown work must remain retained and settled.

This axis-only guard does not admit product-sensitive workgroup-index or global-
invocation-index builtins. The current compiler rejects both; adding them requires
reflected usage and actual product guards, not assuming per-axis limits suffice.
The retained Vulkan indirect native command/synchronization path is preserved,
with the new guard preventing unchecked GPU-generated counts from reaching it.

Nonindexed draw-indirect uses the same full-pass recorder. Supply the exact
`doe_vk_draw_validator_source` shader through the compiler owner, then attach a
`doe_vk_render_indirect` payload to an ordinary draw snapshot. All draw bindings
are prepared before argument copies/guards, after preceding producer commands.
Each indirect draw has distinct command-retained32-byte scratch:16 copied argument
bytes and exact host-produced vertex/instance capacities. The GPU suppresses
over-capacity, wrapping, zero-count and nonzero-firstInstance draws; it never
changes original arguments. No optional indirect-first-instance feature is enabled.
Stride-zero and attribute-free native holes retain their existing semantics.
Only guarded native drawIndirect(count1,stride16) executes inside the pass; guards
and barriers stay outside. Linux presentation is described below.
Host boundary/geometry checks are not shader execution proof. The API preserves
resource-owned pass usage history and failure/uncertain retirement obligations.

### Indexed drawing

A draw snapshot may carry a borrowed `doe_vk_render_indices`: native INDEX buffer,
exact bound offset/size, named Uint16/Uint32 format (native VkIndexType values from
the retained `vk_constants.zig`) and direct index count, first index and signed
base vertex. `draw.vertex_count/first_vertex` must then be zero. The same full-pass
recorder admits the format-aligned offset, Index usage, bound range, context and
content status before any resource preparation, leases the index buffer on the
existing command owner, and records `vkCmdBindIndexBuffer` then `vkCmdDrawIndexed`
inside the one native pass. Direct draws reject firstIndex+indexCount beyond the
bound element capacity. The native bind has no size, so this range check and the
guard below are what enforce the public bound range.

Indexed draw-indirect uses the same validator and source as nonindexed draws. The
source selects the layout from mode word 7: nonindexed scratch is 16 copied bytes,
vertex/instance capacities, zero and mode0; indexed scratch is 20 copied
drawIndexed bytes, index/instance capacities and mode1. The GPU guard suppresses
firstIndex+indexCount beyond the bound index range (compared without forming the
sum), zero counts, instance-step overrun and nonzero firstInstance by zeroing the
copied record; it never changes the caller's arguments. Only guarded
`vkCmdDrawIndexedIndirect(count1,stride20)` executes inside the pass. Index values
are never read on the host or guarded: vertex-step fetches through arbitrary GPU
indices rely on the required robustBufferAccess, so they stay within the native
buffer but not necessarily within the public vertex binding size.

Strip pipelines set `primitiveRestartEnable`, so the all-ones value of the bound
index type restarts the strip; list pipelines never restart and need no optional
list-restart feature. Restart has no effect on nonindexed draws. Uint32 indices
require `caps.full_draw_index_uint32`: context creation enables the native
fullDrawIndexUint32 feature when the device offers it and reports successful
enablement; otherwise Uint32 index draws return UNSUPPORTED instead of exceeding
`maxDrawIndexedIndexValue`. The three native commands are the retained
`vk_functions.zig` declarations; `tools/check_abi.py` verifies each pinned source
line and compiles their signatures plus the two retained index-type constants.

`tests/indexed_contract.c` is a host packet test: exact element capacities, the
feature gate, alignment/usage/range/enum/identity/poison controls, first+count
overflow and boundary, ordered bind-then-draw with a negative base vertex, zero
counts, preflight rollback and the command-owned index lease. Pipeline restart
packets are checked in `depth_pipeline_contract.c`. Neither is GPU execution; the
public native client in `runtime/c/native/tests/indexed_render.c` executes indexed
pixels on an explicitly selected Vulkan adapter.

Viewport admission uses actual device dimensions and bounds, independently of
attachment extent; scissor remains attachment-bound. Negative origins are valid.
A zero logical width or height is implemented by a valid native viewport and
empty scissor while preserving draw/vertex work and occlusion ordering. Public
requested-device limits remain the canonical resource owner's responsibility.

## Linux native surfaces

`doe_vk_surface.h` implements the retained Wayland, XCB and Xlib native-handle
routes. Select exact `surface_platforms` at context creation. The selected graphics
queue must support the actual surface; there is no fallback adapter, queue or
headless image. Instance surface/platform extensions and the full EXT maintenance1
dependency chain are required, followed by actual device feature enablement and
loaded native procedures. `surface_probe` reports discovery only.

Capabilities are bounded (128 formats, 8 present modes/images), with exact requested
format, SRGB nonlinear color space, alpha, usage, extent and present-mode admission.
MAILBOX is native value1 and FIFO_RELAXED value3, correcting reversed constants in
the retained surface translation. Configure requires an explicitly unconfigured
surface, avoiding accidental old-swapchain retirement on failed replacement.

One acquired frame is supported at a time. Acquire uses a real fence; a timeout
retains that acquisition and a retry waits it rather than acquiring another image.
The native acquisition and fence waits are each bounded by the supplied timeout,
so a call may wait up to twice that value. UINT64_MAX acquisition is rejected.
The borrowed frame is valid until present/discard. Old texture/view/command leases
must be released before the next acquisition; stale generations reject recording.

Prepare-present attaches a retained surface to the existing one-shot commands.
Their normal submit records canonical zero-if-undefined and the final PRESENT
transition, then signals a binary semaphore. No second submission engine exists.
The frame is exclusively sealed to that command owner, even after render completion.
Present waits that semaphore and chains a maintenance1 fence. Render/queue-idle
completion never retires presentation resources. A successful present-fence wait
permits semaphore/swapchain retirement, not a claim that pixels were displayed.
Unknown acquire/present completion retains native obligations. Teardown returns
BUSY rather than spinning or freeing them. Known failed present enqueue OOM permits
retry, while other enqueued/uncertain errors retain the fence and original status.
An acquired, quiescent image can be discarded with actual ReleaseSwapchainImagesEXT.
Borrowed platform connections/windows must outlive all this ownership.
`surface_retire_frame` preserves configuration for the normal queue/present loop,
including the original failed-present status after proven retirement.
`surface_retire` adds unconfiguration after that canonical frame cleanup: it retries only an existing acquisition,
waits pending presentation, discards a quiescent frame and unconfigures. It never
starts an acquire or releases caller command/image leases; BUSY preserves them.

Host tests inject actual native-call boundaries and test these states, not display
output. The public native frontend (`runtime/c/native`, `check-present`) now drives
this owner end to end on real windows: on Jupiter (GNOME Wayland with Xwayland),
lavapipe and Radeon 8060S/RADV both acquired, rendered, read back and presented
XCB and Xlib windows (FIFO, including one Timeout retry and a resize that returned
suboptimal) and a role-less Wayland `wl_surface`, with exact pixels read back in the
presenting submission. That proves native acquisition, prepare-present,
present-fence retirement, maintenance1 discard and swapchain teardown on those two
drivers, not what the compositor displayed, OUTDATED/LOST observation on a real
swapchain (host-tested only) or physical hardware qualification. Mesa Wayland
reports 64 surface formats, so the explicit format capacity is 128.
