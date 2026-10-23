# Experimental C D3D12 backend

This is a standalone backend-private C owner, not a production-provider switch
or a complete WebGPU backend. Actual Windows code uses the installed SDK's C
COM declarations. No retained Zig bridge is linked, invoked or removed.

## Implemented boundary

- Exact DXGI adapter ordinal, explicit software-adapter opt-in, D3D12 device,
  direct command queue, private fence and observable adapter identity.
- Default/UAV-capable, zero-initialized upload, and readback buffers; checked
  transfer ranges and scoped map/unmap. Default/readback initialization requires
  a full copy, published only after successful completion.
- Precompiled DXIL/DXBC compute pipelines, immutable structured SRV/UAV layout,
  register spaces, ranged descriptors and three-dimensional dispatch.
- Six explicit texture formats: RGBA8_UNORM, RGBA8_UINT, R32_FLOAT, R32_UINT,
  BGRA8_UNORM and depth-only D32_FLOAT,
  all 2D/single-mip/single-layer/single-sample. Native format capability checks,
  whole-resource upload/readback/copy, immutable same-format SRV/UAV bindings,
  mixed buffer/texture dispatch and copied static nearest/linear samplers.
  D32 requires depth-attachment usage and admits only copy usages in addition;
  it cannot be sampled or stored through shader bindings.
- Offscreen triangle-list graphics with actual VS/PS PSOs, one RGBA8/BGRA8 UNORM
  attachment, optional D32 depth compare/write, explicit clear/load, unconditional
  store and shared sampled/storage bindings. Full-target viewport/scissor, no blend/cull.
- Procedural or typed vertex/instance input: up to eight slots, sixteen TEXCOORD
  attributes, explicit 32-bit float/uint/sint scalar/vector formats, checked direct
  spans and bounded uint16/uint32 indexed views. Layouts are copied at creation.
- Timestamp heaps (1..64 slots), explicit EndQuery and ResolveQueryData into
  readback, real direct-queue frequency and completion-backed slot publication.
- Single-use command recording, early caller release, atomic submission ownership,
  finite waits, partial retirement, unknown-completion recovery and terminal loss.
- Typed statuses, first close failure, allocation rollback and native bounds.

All context calls require caller serialization. Objects keep the context busy,
not independently reference-counted: release every caller object, poll pending
submissions, then call context_destroy. BUSY means the context is still owned by
the caller and must remain available for polling/release. Submitted commands are
context-owned even if the caller releases them. Recording-only commands are
canceled by release. Different pending commands may use disjoint resources; a
buffer, texture or timestamp heap cannot belong to another recorded/unresolved command. This restriction is
explicit, not a claim of full command-buffer or WebGPU concurrency parity.

No depth sampling/stencil/compressed/sRGB texture, mip/array/partial copy, dynamic
sampler/view, indirect draw, base vertex, primitive restart, MSAA/multiple attachments,
presentation, occlusion/pipeline-statistic query, indirect dispatch, uniform-binding,
WGSL-to-DXIL compiler invocation or public WebGPU export is implemented here.
Unsupported broad surfaces are not padded out with success-looking exports.
The native frontend/compiler still owns reflection, shader admission, workgroup
limits, sample/filter compatibility, dynamic offset policy and bounds-proof
obligations. Integer texture SRVs support Load, not filtered sampling.

Full texture uploads/copies initialize texels only after successful completion.
Dispatch does not imply full writes, so sampled and storage textures must already
be initialized. A texture readback does not initialize row padding or bytes outside
the footprint: a padded/partial destination needs a full initializing buffer copy
first, possibly in the same command. Tightly packed whole-buffer copies initialize
the readback directly. Footprints reuse common checked arithmetic; Windows checks
the normalized minimum against actual
[GetCopyableFootprints](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getcopyablefootprints)
before publication. The SDK helper
[uses last-occupied-row sizing](https://github.com/microsoft/DirectX-Headers/blob/main/include/directx/d3dx12_resource_helpers.h),
matching the common arithmetic leaf for this four-byte, single-plane subset.

A clear initializes the full color/depth attachment; draw coverage alone never does.
Geometry requires default-heap initialized buffers, allocation-bounded views and
non-aliasing shader/vertex/index roles. Direct vertex/instance spans are checked
against each view. Indexed vertex values are GPU data: this owner does not scan
or claim to prove index contents. Native view bounds remain in force, with base
vertex fixed at zero and instance step rate one. The native
[vertex view](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_vertex_buffer_view)
carries byte size/stride, and [indexed draw](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawindexedinstanced)
keeps index start and instance start distinct. D32 extent must match color. Depth
transfers copy the entire subresource with zero coordinates and no source box,
following [CopyTextureRegion depth rules](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion).
Graphics resources use both pixel and non-pixel shader read states and return to
COPY_DEST after each draw. RTV, DSV and shader heaps remain owned by the command until
completion or terminal loss. All fallible descriptor preparation precedes command
mutation. Timestamp resolves reject unwritten slots and partial destinations with
uninitialized bytes, following the native
[query resolve obligations](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resolvequerydata).
The frequency is the actual queue frequency; zero/failure is never replaced by 1.
Raw ticks remain raw, without an implicit conversion, calibration or performance
claim. [Microsoft timing semantics](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing).

## Source trace and consolidation

The retained Windows path creates/retains a native runtime in
`runtime/zig/src/native/lifecycle/doe_instance_device_native.zig`. Compute flows
through `native/compute/doe_compute_d3d12.zig` into
`backend/d3d12/d3d12_bridge.c`, with queue/retirement behavior in
`d3d12_native_runtime.zig`, `d3d12_runtime_compute.zig` and
`d3d12_runtime_upload.zig`. Texture allocation/copy/view behavior traces
`resources/d3d12_texture.zig` and the texture/SRV/UAV/sampler operations of
`d3d12_bridge.c`. Graphics traces `commands/d3d12_native_render_pass.zig`
and `commands/d3d12_render_vertex.zig` PipelineKey/ResolvedVertexAttribute, plus
bridge input-layout, IA vertex/index, depth-state and draw operations. The retained
bridge uses ATTR, but this C owner uses TEXCOORD to match the current C HLSL
[location mapping](../../compiler/targets/hlsl/graphics.c); it does not copy that
incompatible retained semantic-name convention. Timestamps trace
`commands/d3d12_gpu_timestamps.zig` and `d3d12_query_set.zig`; this owner does not
copy their separate query command/queue machinery. The new owner consolidates device, ownership, commands
and resource rules by responsibility instead of translating every Zig file.
Retained sources stay live until their consumers are separately migrated; none
is superseded yet, so deleting them would break a current consumer.

Default buffers use the retained COPY_DEST operation boundary. D3D12 buffer
promotion/decay permits the explicit promotable before-state after execution:
[Microsoft resource-state rules](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12).
C descriptor-handle calls use the Windows explicit return-pointer declarations
in [Microsoft DirectX-Headers](https://github.com/microsoft/DirectX-Headers/blob/main/include/directx/d3d12.h).
An actual installed SDK must accept these signatures; documentation inspection
is not SDK compilation. Bounded fence polling avoids a timed-out native event
registration retaining an unsafe event handle. Its performance is unqualified.

## Checks

From this directory:

    make check
    ASAN_OPTIONS=detect_leaks=0 make sanitize

These compile the portable owner with strict C11 warnings and inject failures at
its private typed native-call boundary. The mock does not execute copies or
shaders. Its read pattern tests mapping eligibility only. Allocation accounting
checks owner teardown. LeakSanitizer is unavailable under this executor's ptrace;
ASan/UBSan can run with leak detection disabled, which is not an LSAN pass.

On Windows in a configured MSVC/Windows SDK developer command prompt:

    build_windows.cmd
    dxc -T cs_6_0 -E main tests\native_compute.hlsl -Fo "%TEMP%\doe-c-d3d12\native_compute.dxil"
    dxc -T cs_6_0 -E main tests\native_texture.hlsl -Fo "%TEMP%\doe-c-d3d12\native_texture.dxil"
    dxc -T vs_6_0 -E vertex_main tests\native_graphics.hlsl -Fo "%TEMP%\doe-c-d3d12\vertex.dxil"
    dxc -T ps_6_0 -E fragment_main tests\native_graphics.hlsl -Fo "%TEMP%\doe-c-d3d12\fragment.dxil"
    dxc -T vs_6_0 -E vertex_main tests\native_mesh.hlsl -Fo "%TEMP%\doe-c-d3d12\mesh_vertex.dxil"
    dxc -T ps_6_0 -E fragment_main tests\native_mesh.hlsl -Fo "%TEMP%\doe-c-d3d12\mesh_fragment.dxil"

The build script only builds. After separate execution authorization:

    "%TEMP%\doe-c-d3d12\native_compute.exe" "%TEMP%\doe-c-d3d12\native_compute.dxil"
    "%TEMP%\doe-c-d3d12\native_texture.exe" "%TEMP%\doe-c-d3d12\native_texture.dxil"
    "%TEMP%\doe-c-d3d12\native_graphics.exe" "%TEMP%\doe-c-d3d12\vertex.dxil" "%TEMP%\doe-c-d3d12\fragment.dxil"
    "%TEMP%\doe-c-d3d12\native_mesh.exe" "%TEMP%\doe-c-d3d12\mesh_vertex.dxil" "%TEMP%\doe-c-d3d12\mesh_fragment.dxil"

A Windows-target C compiler with the actual SDK may alternatively use
`make windows` and `make native-fixture`. Non-Windows `make` runs only host
checks; there is no fake production D3D12 implementation on Linux. The native
fixture releases caller upload/storage/pipeline references before submission,
then releases the caller command before polling and compares real readback. The
texture fixture samples a 3x2 R32_FLOAT texture through a static sampler, writes
a storage texture, copies to another texture and checks every byte of padded
readback against binary-exact values, including preserved padding. The graphics
fixture draws a sampled green fullscreen triangle over a red clear and checks
exact RGBA bytes plus real timestamp ordering/frequency, releasing caller texture,
query, upload, pipeline and command references before completion. The mesh fixture
draws indexed blue-near then red-far triangles with per-instance color and LESS
depth testing. It checks exact BGRA blue bytes and every D32 value at 0.25 after
early caller geometry/attachment/pipeline/command release. No native fixture
executes as part of host checks.

Windows SDK compilation, DXC compilation, native driver readback, debug-layer
validation and physical qualification are **unrun** in the dot cloud environment.
The fixture's portable syntax check is not any of those qualifications.

Core SOURCE_INPUTS.sha256 / TERMINAL_RECEIPT.txt, TEXTURE_* and GRAPHICS_* seals
are historical. Current mesh/depth inputs and checks are in MESH_SOURCE_INPUTS.sha256
and MESH_TERMINAL_RECEIPT.txt; earlier evidence does not qualify changed bytes.
