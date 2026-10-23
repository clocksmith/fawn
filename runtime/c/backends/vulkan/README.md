# Experimental C Vulkan backend

This private C11 owner implements real Linux LP64 Vulkan 1.2 discovery, device and
queue selection, capabilities, host-coherent and device-local buffers, sparse
multi-group buffer/image/sampler descriptors, SPIR-V compute pipelines, copy/fill and
direct dispatch recording, asynchronous fence submission, readback and retirement.
Single-color offscreen rendering shares those allocation and completion owners.
The texture owner adds uncompressed color images, retained views/samplers and
mip/layer-aware padded transfers across 1D/2D/3D images.
It is not wired into the production provider. It is not full Vulkan/WebGPU parity.

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
allocations are initialized. Robust buffer access is required/enabled. Only core
baseline shader features are enabled; unsupported feature use must be rejected by
the compiler/native admission path before creating a pipeline. This is not a
complete public validation boundary for untrusted SPIR-V.

GPU-produced indirect counts, surfaces,
prepared-program caches and tuned hazard elision are not yet migrated here.
Indirect recording rejects explicitly rather than dispatching unchecked counts.
The retained production owners remain authoritative for those behaviors.

## Texture transactions

`include/doe_vk_texture.h` uses named native format values, never WebGPU numeric
casts. Eighteen uncompressed color formats are currently admitted; depth/stencil,
compressed formats, multisampling, reinterpretation, swizzles and cube-array
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

Pipeline state admits sample1, one color target, eight vertex layouts/sixteen
32-bit scalar/vector attributes, native blend/cull/topology controls and dynamic
viewport/scissor. Blend constants are immutable pipeline state. Depth/stencil,
MSAA, multiple attachments, indexed/indirect draws, dynamic blend constants,
and discard-store semantics are not implemented by this interface.
Unsupported resource packets must be rejected, not silently dropped. Graphics
requires `require_graphics_queue=true` at context creation when the device offers
separate compute-only queues; default compute queue selection is unchanged.

`make native-render VERTEX_SPIRV=... FRAGMENT_SPIRV=...` uses the two retained Tint
fixtures bound in `SMOKE_IMAGE_FIXTURES.json`. Blue clear plus two preserving draws
with vertex attributes, uniform data and disjoint scissors produces 64 exact RGBA8
pixels. It releases caller references before submission. The bound older C compiler
rejects graphics entrypoints; this test is real Vulkan graphics, not C graphics
compiler evidence or physical qualification.

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
records, including retained unions, plus nine format/query constant assertions verified against the linked Khronos
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
