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

GPU-produced indirect counts, surfaces, occlusion queries,
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

`include/doe_vk_render.h` records a clear-only packet and direct instanced draw
packets into the existing command owner. Each draw loads/stores the single color
attachment, preserving the resource owner's first-pass clear and subsequent draw
ordering. Exact views, compatible render passes/framebuffers, descriptors and
vertex buffers remain retained until native retirement. A clear initializes only
the selected full 2D mip/layer; a draw cannot read uninitialized target contents.

Pipeline state admits sample1, one color target, eight vertex layouts/sixteen
32-bit scalar/vector attributes, native blend/cull/topology controls and dynamic
viewport/scissor. Blend constants are immutable pipeline state. Depth/stencil,
MSAA, multiple attachments, indexed/indirect draws, dynamic blend constants,
occlusion queries and discard-store semantics are not implemented by this interface.
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
results or physical qualification. Occlusion queries are not implemented yet.

## ABI provenance and checks

No SDK headers were available. The selected declarations are a narrow translation
of the existing source hashes in `ABI_PROVENANCE.json`, not invented opaque-sized
struct storage. `tools/check_abi.py` refuses source drift and independently models
the pinned extern fields to compile 1344 size/offset/alignment assertions over 76
records, including retained unions, plus eight format-bit assertions verified against the linked Khronos
reference. No SDK or other dependency was downloaded. It does not prove the retained declarations against an official SDK.
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
