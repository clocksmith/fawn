# Experimental C D3D12 backend

This is a standalone backend-private C owner, not a production-provider switch
or a complete WebGPU backend. Windows execution uses the installed SDK's C COM
declarations; the narrow shader adapter uses official DXC C++ interfaces behind
a C ABI. No retained Zig bridge is linked, invoked or removed.

## Implemented boundary

- Exact DXGI ordinal/LUID, first hardware-class member or explicit CPU WARP
  selection, software-adapter opt-in, D3D12 device,
  direct command queue, private fence and observable adapter identity.
- Default/UAV-capable, zero-initialized upload, and readback buffers; checked
  transfer ranges and scoped map/unmap. DEFAULT initialization uses a full copy,
  published as completed after successful completion; known-fenced acceptance
  separately authorizes future GPU use on the same queue. Ordinary READBACK
  creation remains uninitialized; its additive zeroed constructor finishes full
  CPU initialization before publication.
- Precompiled DXIL/DXBC compute pipelines, immutable structured SRV/UAV layout,
  register spaces, ranged descriptors and three-dimensional dispatch.
- GPU-owned indirect compute records with checked allocation/range/alignment,
  no binding aliases, a context-owned DISPATCH command signature and the existing
  command/resource retirement owner. The caller/frontend proves each group count
  at most 65535; this private backend neither reads nor clamps GPU arguments.
- Explicit read-only uniform CBVs in the same immutable layout and descriptor
  heap as structured SRVs/UAVs. The shared descriptor-kind owner preserves b/t/u
  register namespaces, and graphics/compute share range and lifetime admission.
- Six explicit texture formats: RGBA8_UNORM, RGBA8_UINT, R32_FLOAT, R32_UINT,
  BGRA8_UNORM and depth-only D32_FLOAT,
  all 2D/single-mip/single-layer/single-sample. Native format capability checks,
  whole-resource upload/readback/copy, immutable same-format SRV/UAV bindings,
  mixed buffer/texture dispatch and copied static nearest/linear samplers.
  The additive [dynamic binding contract](include/doe_d3d12_dynamic.h) admits
  sampled float/uint classes, exact-format read-storage SRVs and runtime sampler
  values on a separate descriptor heap. Independent min/mag/mip, U/V/W address,
  LOD, anisotropy and comparison fields are copied per record. Legacy exact-format
  and static-sampler layouts retain their original representation. Compatible
  sampled/read-storage aliases transition and retain each texture once; any
  writable alias rejects before native recording.
  D32 requires depth-attachment usage and admits only copy usages in addition;
  it cannot be sampled or stored through shader bindings.
- Offscreen triangle-list graphics with actual VS/PS PSOs, one RGBA8/BGRA8 UNORM
  attachment, optional D32 depth compare/write, explicit clear/load, unconditional
  store and shared sampled/storage bindings. Full-target viewport/scissor, no blend/cull.
- Procedural or typed vertex/instance input: up to eight slots and sixteen
  TEXCOORD attributes. The whole-pass route converts all 41 canonical formats
  through real SDK identities and required native input-format queries. Its new
  source preserves holes, empty layouts, zero strides and checked direct spans;
  a missing native format is a typed unsupported result, not an alternate fetch.
  Legacy mesh controls retain bounded uint16/uint32 indexed views. Layouts are
  copied at creation; broad format execution requires separate evidence.
- Timestamp heaps (1..64 slots), explicit EndQuery and ResolveQueryData into
  readback, real direct-queue frequency and completion-backed slot publication.
- Single-use command recording, early caller release, atomic submission ownership,
  finite waits, partial retirement, unknown-completion recovery and terminal loss.
  Successful fence retirement may retain one closed native allocator/list pair on
  the existing context. Later commands reset it in allocator/list order; failures
  propagate without a fresh-create retry. Pending/lost/discarded recordings never
  supply a pair, and context teardown releases its idle native objects.
- Caller-data writes may reuse one ordinary UPLOAD page up to one MiB on the
  context. No other caller, recorded or pending command may still hold it. Busy
  and larger writes allocate independent storage; immutable zero data stays
  separate. A speculative larger page's OOM alone permits an exact-size retry.
  Initial allocation zeros its full width; reuse copies only caller bytes and
  makes no zero-content promise. Teardown drops only the cache's ordinary lease.
- Compute and graphics resource descriptors borrow contiguous ranges of a
  context-owned 4096-entry shader-visible heap instead of creating a heap per
  descriptor use. The first heap is created by the first descriptor preparation and
  lives with the context; a range returns only when its command is discarded,
  terminally lost or successfully retired, so no CPU descriptor write overlaps GPU
  use. Overflow creates another heap that releases once empty. Sampler heaps and
  RTV/DSV/clear views keep their per-use owners. `test_windows_descriptor_ring.c`
  is the official-SDK CPU control.
- Buffers whose queried allocation is at most 1 MiB are placed resources in
  context-owned 4 MiB `ALLOW_ONLY_BUFFERS` heaps, one memory class per heap and
  64 KiB slots; larger buffers stay committed. Placement does not change any
  initialization rule: heap zeroing is never relied on, and DEFAULT, UPLOAD and
  zeroed READBACK construction still initialize explicitly. A slot range returns
  only when its resource is released (after the common owner's retirement), one
  empty heap per class is kept, and context close detaches heaps that still hold
  placements so their last buffer releases them. Heap or placement failure keeps
  its exact status without a committed retry. `test_windows_buffer_heaps.c` is the
  official-SDK CPU control; it proves call composition only.
- Typed statuses, first close failure, allocation rollback and native bounds.

The additive [whole-pass render API](include/doe_d3d12_render.h) composes
independent VS/PS layouts with stage-visible root tables. Its two immutable stage
maps preserve 64 resource rows and 16 runtime sampler rows each, packing 128/32 into
one resource heap and one sampler heap per command descriptor use. Compute keeps
its existing 64/16 limits. Module-local resource ordinals never identify another
stage's resource. Compatible read aliases share allocation leases and transitions.

The public graphics bridge composes all five direct primitive topologies to one
RGBA8/BGRA8 UNORM color target at sample 1, with frontFace, culling, viewport and
scissor. Explicit zero sample/color-write masks are preserved, not defaulted.
Blend factors/operations use the shared frontend validation and native PSO state;
each draw records its own blend constant. Optional render-only D32_FLOAT targets
compose depth compare/write/bias and whole-pass clear/load/discard using the
existing DSV and texture-use owners. Texture creation performs real zero upload.
The source also includes direct vertex/instance inputs for all 41 canonical formats,
with nonzero ranges, native default component fill and per-record read aliases.
Public required empty slots remain the resource owner's admission obligation;
backend holes never invent a buffer. This new increment has not had an SDK or
driver run. Portable owner controls cover the new fixed-state admission, zero
masks, blend code rejection, D32 initialization/clear/discard, preparation failure
and early-release lifetimes. The native descriptor control retains its prior
unsupported sampled-depth cases and separately admits render-only D32. These
controls are not SDK compilation, device execution or pixel qualification.

Whole passes execute empty/zero-draw CLEAR/LOAD/STORE. Discard records a real zero
clear at pass end, so later reads observe initialized zeros through the existing
texture-use owner; no frontend availability bit is added. All native heaps/views
prepare before recording. Ordinary command/pending leases retain every dependency
through unknown completion. This is a checkpoint toward retained graphics parity,
not a narrower final acceptance target. Eight MRT slots, broader color formats,
stencil, MSAA and public fragment UAV writes remain concrete
migration work. Guarded indexed/indirect source composition is described below;
its device qualification remains open. The retained
[all-format consumer](../../native/tests/vertex_formats.c) additionally needs
RGBA32 float/uint/sint targets; its acceptance is unchanged by the new RGBA8 fetch control.

`tests/test_render.c` exercises 128/32 stage capacity, immutable values, empty and
zero draws, preparation failures and early-release/unknown leases at the portable
call seam. `tests/test_windows_render.c` checks real SDK root serialization and
native typed call composition without creating a device. The newer whole-pass
source evidence is bound separately from later vertex changes. The vertex
increment's `tests/test_render_vertices.c` exercises typed formats, holes, zero
stride, direct spans, aliases and unknown leases; the SDK CPU control checks
actual format identities/queries and native views. None establishes fetched
pixels. Prior static-graphics receipts remain historical evidence for their exact
sources.

All context calls require caller serialization. Objects keep the context busy,
not independently reference-counted: release every caller object, poll pending
submissions, then call context_destroy. BUSY means the context is still owned by
the caller and must remain available for polling/release. Submitted commands are
context-owned even if the caller releases them. Recording-only commands are
canceled by release. Buffers keep one exclusive unsubmitted recording reservation.
Known-fenced acceptance releases it for later queue-ordered GPU uses, retaining
one existing reference per command through retirement. A borrowed latest pending
command and its accepted initialization proof authorize GPU recording; CPU maps
and writes still reject pending use. Completed initialization remains separate,
including partial retirement, lost devices and signal recovery. Textures and
timestamp heaps retain exclusive recorded/unresolved ownership. This is not full
command-buffer or WebGPU concurrency parity.

No depth sampling/stencil/compressed/sRGB texture, mip/array texture or general view, indirect draw, base vertex, primitive restart, MSAA/multiple attachments,
presentation, occlusion/pipeline-statistic query,
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

Indirect records contain three uint32 dimensions and use one native
[ExecuteIndirect](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect)
operation at a four-byte-aligned argument offset. The argument allocation is
DEFAULT memory, fully initialized and separately retained from shader bindings.
COPY_DEST/INDIRECT_ARGUMENT transitions use the existing ordered command list;
unknown signal/completion keeps the arguments, descriptors, pipelines and command
alive. Zero dimensions retain the native no-work semantics. This is not public
WebGPU indirect-argument robustness or proof of the GPU-generated dimension limit.

Uniform layouts select `DOE_DX_UNIFORM`, read-only access and zero structured
stride. Bindings are nonempty, offset and size are multiples of 256, and the size
is at most 65536. The physical view, including padding, must fit and already be
initialized; no implicit rounding or memory outside the bound view is exposed.
Native lowering creates a
[constant-buffer view](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_constant_buffer_view_desc)
and uses the constant-buffer resource state, with the
[SDK constants](https://learn.microsoft.com/en-us/windows/win32/direct3d12/constants)
governing alignment and capacity. Logical WGSL lengths and packed layout remain
compiler/frontend obligations. Same-number b/t/u bindings are distinct; duplicate
bindings in the same register space and namespace are rejected.

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
is not SDK compilation. Bounded waits use one context-owned fence event;
timeout or wait failure retains its registration and every resource obligation.
A repeated wait consumes the existing signal before registering newer work.
Only queried completion or terminal device loss settles work. Destruction drains
an outstanding terminal event notification before closing its handle; a failed
drain/close returns the existing failure status and retains the context for retry.
Held event leases additionally permit read-only fence observation outside context
serialization. The existing context object obligation protects native handles after
a command shell retires. Active observers cannot be closed; timed-out registrations
remain context-owned and are reused for that fence or drained after terminal work.
These call-composition guarantees do not establish physical performance.

## Official DXC SDK adapter

[The shader boundary](include/doe_d3d12_shader.h) accepts exact HLSL bytes, an
explicit entry and stage, an optional compiler-owned `DOE_HLSL_ENTRY` index, and
a fully qualified UTF-8 `dxcompiler.dll` path. It generates no HLSL or reflection.
`windows_shader.cpp` uses the official
[IDxcCompiler3/IDxcResult interfaces](https://github.com/microsoft/DirectXShaderCompiler/blob/v1.9.2609/include/dxc/dxcapi.h),
with exceptions and RTTI disabled, to compile cs/vs/ps Shader Model 6.0 under
`-HV 2021 -Ges -WX -Gis`. No caller flags, subprocess, include handler or filesystem
include capability is supplied. The compiler/frontend still owns entry metadata,
workgroup limits and native feature admission, including wave requirements.

Failure leaves the caller's bytecode output untouched. Operation, signed native
code and a bounded complete UTF-8 diagnostic prefix are copied before releasing
DXC results. Successful bytecode owns its `IDxcBlob` and loaded module until
release; views borrow immutable bytes. The SDK call preserves caller errno,
LastError, standard fenv and x86 MXCSR, without claiming WGSL numeric normalization.
An explicit module is loaded with its directory and system-directory dependency
search only; there is no ambient compiler lookup or alternate compiler fallback.

`build_shader_windows.cmd` requires an actual Windows SDK developer environment,
`OUT`, and `DXC_ROOT` naming a previously verified official portable release.
The pinned release is [DXC 1.9.2609.5](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609),
archive `dxc_2026_09_29.zip`, SHA256
`ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1`.
Headers and binaries stay outside Git. The route records actual module version,
module/header hashes, then runs the C ABI CPU control: valid cs/vs/ps objects,
wrong entry/stage/index, invalid source, an existing-file include rejection,
diagnostic truncation/copy, unchanged failure output, source/module lifetime and
perturbed host-state preservation. No device is created or GPU shader executed.
Authored controls and cross-compilation are not an actual Windows SDK execution
pass; public WGSL/provider integration, hardware and production promotion retain
their separate gates.

## Checks

The zero/default `DOE_DX_SELECT_ORDINAL` still selects one exact DXGI ordinal.
`DOE_DX_SELECT_WARP` explicitly chooses `EnumWarpAdapter`; it also requires
`allow_software_adapter=true` and ordinal zero. Unknown/conflicting options reject
before allocation or a native call. WARP selection verifies the observed DXGI
software flag, publishes the actual adapter name/vendor/device/LUID and reports
`DOE_DX_NO_ADAPTER_ORDINAL` because it did not enumerate an ordinal. Failed
selection never tries another adapter. See Microsoft's
[explicit WARP adapter contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgifactory4-enumwarpadapter).

The native compute client accepts a final `--warp` argument, including alongside
its existing uniform/indirect modes. That mode checks explicit software identity
and retains the existing 16-word integer oracle, early caller releases, 30-second
polling bound and required successful context destruction. It consumes validated
shader bytecode, not a public WGSL/WebGPU frontend or a host shader interpreter.
CPU WARP execution is a real Windows driver check, not physical GPU qualification.

`run_warp.cmd` is the bounded hosted-control helper. Call it from this owner
directory after `VsDevCmd.bat` and `build_windows.cmd`, with `OUT` naming the
existing build and `WARP_EVIDENCE` a dedicated evidence directory. It requires
exactly `%WindowsSdkVerBinPath%x64\fxc.exe`, compiles `tests/native_compute.hlsl`
with `/Ges /WX /T cs_5_1 /E main` to `shader.dxbc`, and runs only
`native_compute.exe shader.dxbc --warp`. It records the resolved compiler path,
shader and compute logs, refuses its own existing evidence files and propagates
every compiler/client failure. It installs nothing and selects no alternate
compiler/adapter. The root-owned optional
[hosted workflow](../../../../.github/workflows/c-d3d12-sdk-build.yml) supplies
the outer process-tree deadline and retains source/tool/artifact identities.

From this directory:

    make check
    ASAN_OPTIONS=detect_leaks=0 make sanitize

These compile the portable owner with strict C11 warnings and inject failures at
its private typed native-call boundary. The mock does not execute copies or
shaders. Its read pattern tests mapping eligibility only. Allocation accounting
checks owner teardown. Historical cloud checks disabled unavailable LeakSanitizer;
that is not an LSAN pass. The current Apple-clang combined ASan/UBSan executable
stalled before `main` in ASan shadow/allocator initialization and was stopped.
UBSan-only developer checks passed separately; neither outcome is an ASan pass.

On Windows in a configured MSVC/Windows SDK developer command prompt:

    build_windows.cmd
    dxc -T cs_6_0 -E main tests\native_compute.hlsl -Fo "%TEMP%\doe-c-d3d12\native_compute.dxil"
    dxc -T cs_6_0 -E main tests\native_indirect.hlsl -Fo "%TEMP%\doe-c-d3d12\native_indirect.dxil"
    dxc -T cs_6_0 -E main tests\native_uniform.hlsl -Fo "%TEMP%\doe-c-d3d12\native_uniform.dxil"
    dxc -T cs_6_0 -E main tests\native_texture.hlsl -Fo "%TEMP%\doe-c-d3d12\native_texture.dxil"
    dxc -T vs_6_0 -E vertex_main tests\native_graphics.hlsl -Fo "%TEMP%\doe-c-d3d12\vertex.dxil"
    dxc -T ps_6_0 -E fragment_main tests\native_graphics.hlsl -Fo "%TEMP%\doe-c-d3d12\fragment.dxil"
    dxc -T vs_6_0 -E vertex_main tests\native_mesh.hlsl -Fo "%TEMP%\doe-c-d3d12\mesh_vertex.dxil"
    dxc -T ps_6_0 -E fragment_main tests\native_mesh.hlsl -Fo "%TEMP%\doe-c-d3d12\mesh_fragment.dxil"

The build script only builds. After separate execution authorization:

    "%TEMP%\doe-c-d3d12\native_compute.exe" "%TEMP%\doe-c-d3d12\native_compute.dxil"
    "%TEMP%\doe-c-d3d12\native_compute.exe" "%TEMP%\doe-c-d3d12\native_compute.dxil" "%TEMP%\doe-c-d3d12\native_indirect.dxil"
    "%TEMP%\doe-c-d3d12\native_compute.exe" "%TEMP%\doe-c-d3d12\native_uniform.dxil" --uniform
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

The compute fixture also accepts an indirect producer shader. That shader writes
the three group counts into argument words 4..6 on the GPU; the fixture checks
all target output words and every argument word after native indirect consumption.
It drops producer, argument and target callers before submission. Its `--uniform`
variant consumes the same arithmetic through a 256-byte b0 uniform view, keeping
logical parameters and initialized padding explicit. Both are authored real-driver
consumers with physical developer execution recorded below; host syntax checks
alone never establish their driver behavior.

The original dot host evidence did not run Windows SDK compilation, native driver
readback or debug-layer validation. Later source-bound SDK and CPU WARP evidence
is separate below; portable syntax checks never establish those properties or
physical qualification.

Core SOURCE_INPUTS.sha256 / TERMINAL_RECEIPT.txt, TEXTURE_*, GRAPHICS_* and MESH_*
seals are historical. [The indirect/uniform host receipt](DEVELOPER_RECEIPT_INDIRECT_UNIFORM.json)
binds 49 exact owner inputs, unchanged across strict and UBSan-only host checks:
47 groups cover core, texture, graphics, mesh, indirect and uniform behavior.
It records the ASan startup block and unrun SDK/DXC/driver boundaries explicitly.
Earlier evidence never qualifies changed bytes.

[Actual Windows SDK developer build](DEVELOPER_RECEIPT_WINDOWS_SDK.json) at
`a29e2fec5` compiles the complete private backend and links all four native
clients with MSVC14.51/SDK10.0.26100 under `/std:c11 /W4 /WX`.
The first SDK failure was fixed by replacing an unused legacy compiler include
with the required common SDK types. All30 output hashes verify. No client was
executed; device, public frontend and physical qualification remain separate.

[Actual CPU WARP developer execution](DEVELOPER_RECEIPT_WARP.json) at
`159b58a18` compiles the private backend and all four native clients with the
actual Windows SDK, then runs only the direct compute client. SDK FXC 10.0.26100.9457
compiles the exact HLSL fixture under `/Ges /WX /T cs_5_1`. The client observes
Microsoft Basic Render Driver, vendor 5140/device 140, software flag 1, explicit
WARP selection and no ordinal. All 16 readback words match the host arithmetic
oracle after upload, dispatch and copy; early caller releases and successful
context destruction pass. The receipt binds 38 exact Git source blobs, the
[hosted run](https://github.com/doerun/doe/actions/runs/40040368403), tool identity,
artifact ZIP and 39 verified build/control hashes. Raw output words were compared
by the client and are not retained separately. This is CPU driver correctness
evidence for that direct fixture; it does not establish physical GPU, indirect,
uniform, graphics, public D3D12 frontend, performance or release qualification.

[Miles physical developer execution](DEVELOPER_RECEIPT_MILES_D3D12.json) runs the
unchanged source `159b58a18` SDK-built clients on AMD Radeon(TM) Graphics,
vendor 4098/device 5761, driver 32.0.21030.2001, Windows 11 build 22631. Every
client reports software flag 0 and exact ordinal 0. All six controls pass:
16-word compute, GPU-generated indirect arguments, uniform buffers, sampled and
storage texture transfers with preserved padding, sampled RGBA drawing with
timestamp ordering, and indexed instance drawing with exact BGRA/depth readback.
Each requires completion after early caller releases and successful context
destruction. Official portable DXC 1.9.2609.5 compiles all eight shader stages
under `-Ges -WX`; its archive matches Microsoft's published SHA256. The receipt
binds 43 source blobs, binary and shader hashes, commands, actual adapter/driver,
120-second process guards, and 97 verified evidence files. The isolated per-user
run changes no existing checkout, PATH, SDK installation or execution policy.
This is bounded private-backend hardware evidence for those cases, with no public
D3D12 frontend, presentation, performance or release qualification claim.

## Exact identity and queried admission bounds

`DOE_DX_SELECT_HARDWARE` requires ordinal zero and an empty identity. It skips
actual DXGI software entries, selects the first nonsoftware member, and never
tries another member after device creation or a query fails. An empty hardware
class returns UNAVAILABLE. The legacy zero/default ordinal mode remains exact,
and WARP still requires explicit selection and software opt-in.

Device reopening uses `DOE_DX_SELECT_LUID` and `adapter_identity`, with both LUID
words and the expected software flag. The actual
[EnumAdapterByLuid](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgifactory4-enumadapterbyluid)
result is checked against GetDesc1; a missing or mismatched identity fails without
ordinal/WARP retry. The created device's own GetAdapterLuid must match too. A LUID
is a current host identity, not a persistent cross-reboot device identifier.

Caps contain actual required feature-level (requested FL11_0..FL12_1), resource
binding/heap tier, GPU address-width and node0 UMA query facts. Query failure
rejects construction. Shader-model queries ask up to SM6_0; an older runtime's
E_INVALIDARG is reported as `shader_model=0` (unknown), preserving DXBC use without
inventing DXIL eligibility. Other query failures preserve their HRESULT.

`limits` distinguishes backend admission from those raw facts. One CBV/SRV/UAV heap/table per use admits up to 64 descriptors, with existing command-owned
retirement. The separate static-sampler limit remains 16. Cumulative command uses grow
within the same owner rather than imposing a 64-resource limit. Per-kind counts are checked before native preparation using actual
[resource binding tiers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-support):
Tier 1/2 CBVs 14, Tier 1 SRVs 128 (clamped to 64), Tier 1 FL11_0 UAVs 8, otherwise
UAVs 64; Tier 3 CBVs are clamped to 64. CBV/SRV limits apply per stage to actual
descriptor tables. Hidden root constants and root CBVs consume the separate root
signature DWORD budget. UAV descriptors are counted across both actual graphics
stage tables; a duplicated descriptor really consumes two entries. This is not complete WebGPU graphics admission.

After actual FL11_0+ admission, SDK guarantees supply compute threads 1024,
axis sizes 1024/1024/64, dispatch groups 65535 per axis, shared memory 32768 bytes,
CBV offset alignment 256 and CBV size 65536. They are not separately queried
optional features or a WebGPU CoreFeatures claim. The compiler/frontend still
proves workgroup dimensions, total threads, shared memory and indirect counts.

The single-buffer ceiling is an explicit 256 MiB implementation bound clamped by
actual resource/process GPU-VA widths and SIZE_MAX. A matching UAV-buffer
GetResourceAllocationInfo query must return a valid non-sentinel allocation
size/alignment within that address bound before caps publish. Each real buffer
allocation rechecks its own queried allocation cost against the admitted ceiling.
This is not a residency budget, free-memory measurement or allocation guarantee;
CreateCommittedResource can still fail. Uniform logical size/padding policy
remains upstream. No buffer range is widened implicitly. `context_status` reads
existing terminal state without polling, consuming an error or settling a lease.

`make check` now also covers wrong LUID words/class, no hardware, narrowed
reported descriptor/buffer/dispatch bounds, 40 mixed resource descriptors and 64
initialized buffer bindings through early release and actual owner retirement.
These are programmed native-call facts, not hardware queries or shader execution.
`build_windows.cmd` additionally builds `test_windows_caps.exe`; the root hosted
workflow runs it explicitly. This CPU control uses official SDK vtable types to
exercise required-query failures, tier derivation, unknown shader-model query,
VA clamping, bad allocation size/alignment and exact hardware/LUID selection. It
creates no device and establishes no real driver capability. Historical source
receipts above do not qualify this later source.

## Immutable zero-upload reuse

`doe_dx_zero_upload_acquire` returns a retained immutable zero-filled upload
source. The existing context keeps at most one source up to one MiB; growth
publishes only after the replacement is completely initialized, and larger
requests use uncached storage. Existing caller/command references keep an older
source alive across replacement and unlocked initialization waits. Host writes
to these sources reject; caller data always uses separate mutable staging.

The frontend copies zeros over each whole DEFAULT resource, including uniform
padding. Creation defers that copy: the device keeps an exact list of DEFAULT
buffers not yet initialized, and before any later D3D12 recording (queue submit,
texture/surface work, internal transfers) or CPU read it records every listed fill
into one command and submits it. Only a queue write covering the whole allocation,
padding included, removes a buffer without a fill, because that copy initializes
every byte; a partial write materializes the fills first. A buffer released
before any observation needs no fill. The fill returns after known-fenced queue
acceptance. The common pending
owner retains that initialization until terminal observation; CPU access still
waits for the buffer's actual owner. Reuse changes allocation ownership, not
initialized ranges or GPU ordering. Context
destruction releases its cache lease; outstanding caller, command and event
obligations still refuse destruction. `test_zero_upload.c` checks these owner
edges without executing data. `native_zero_upload.c` separately checks actual
GPU zero copies and readback over small, padded, cached and uncached sizes.

`doe_dx_buffer_create_readback_zeroed` separately performs a full CPU zero fill
of READBACK resource Width before publication. Ordinary private READBACK creation
remains uninitialized and ordinary writes remain UPLOAD-only. This uses
[READBACK write-back memory](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_heap_type)
under the [Map ordering contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map):
CPU writes finish before any GPU command can reference the new resource. Written
Unmap ranges are tooling metadata, not synchronization. Later GPU writes still
require completion before CPU mapping. The host and official-SDK controls keep
their separate scopes; the physical gated initialization fixture checks full
zeros, a partial copy across a cache-line boundary, padded DEFAULT initialization,
early release and loss before initialization completion.

## Buffer aliases within one command

Repeated read-only views of one buffer may overlap, including CBV plus structured
SRV views. Disjoint writable UAV ranges may share that buffer too; overlapping
writable ranges and read/write class mixing reject before native recording.
An unsubmitted command retains an exclusive buffer reservation. After known-fenced
acceptance, later buffer uses follow the same native queue order; CPU access stays
excluded until all accepted uses retire. Texture, geometry and indirect-argument
alias admission within each command remains unchanged.

Each unique buffer consumes one existing command resource slot and lease. Native
barriers combine compatible CBV/SRV read-state bits and transition/restore once
per allocation, while descriptors retain each view's exact range. There is no
shadow allocation or additional lifetime list. `test_aliases.c` covers read views,
disjoint/overlapping UAVs, bad ranges, mixed classes, the last resource slot,
early caller release and unknown completion. The SDK CPU call control observes
actual lowering's barrier count, identity and state bits, including graphics
read bits and repeated UAVs. These controls do not execute a shader; actual
public WARP consumption remains separate.

## Growing command-use storage

The command remains the only resource-retirement owner. Its existing buffer,
texture, query and pipeline use records now occupy checked growable spans; there
is no cumulative 64-resource or 65536-operation admission limit. Buffer, texture
and query objects retain an exclusive recording pointer plus a stable slot index;
buffers additionally borrow their latest known-fenced pending identity. Lookup is
constant time even when storage moves; no caller retains
a use-record address across a reserve. Per-dispatch descriptor capacity remains
64 and static samplers remain 16. Those pipeline bounds do not cap the number of
objects a sequence of commands may use.

All required span capacity is reserved before recording native work or publishing
counts, initialization or leases. Growth checks count/byte arithmetic and uses
calloc/copy/free, preserving the existing allocation-fault seam. A failed later
reserve may leave prepared capacity owned by the command, while use counts and
references remain unchanged. Failure reports actual allocation/addressability
OOM; retry or discard releases every prepared allocation.

Shared-read pipelines take one existing reference per successful draw/dispatch
record, appended without a repeated ownership search. Retirement/discard drops
each such reference exactly once. Exclusive resource uses remain deduplicated.
There is no additional registry, queue or reference-count owner. The test native
call seam records borrowed native occurrences only; it executes no data copy or
shader.

`test_dynamic.c` checks 65/129 unique buffers, 129 texture/query indices, 65537
repeated pipeline records, partial multi-span allocation failure, actual recording
OOM/retry and unknown-completion retention after early caller release. Active
former-budget cases now test genuine reserve failures rather than preserving the
removed artificial rejection. Historical sealed receipts remain unchanged and do
not qualify this later source, Windows SDK composition or physical execution.

## Typed color-region transfers

`include/doe_d3d12_texture_regions.h` extends the existing command owner with
partial single-mip/layer 2D color transfers and blits. Five admitted four-byte
color formats reuse actual allocation-time format/footprint queries. Depth remains
excluded from this region API. The legacy whole-resource API remains available.

Region uploads read initialized UPLOAD or DEFAULT buffers. DEFAULT sources make
real COPY_DEST -> COPY_SOURCE -> COPY_DEST transitions. Region downloads write
READBACK or DEFAULT buffers; partial/padded destinations must already be
initialized. Partial uploads and all region blits require initialized destination
textures, preserving their prior initialization without inferring shader coverage.
All range, heap, alias and initialized-state checks and command span reserves
precede native recording and lease publication. Completion remains on the existing
context/command owner, including unknown work after caller release.

`test_texture_regions.c` exercises pure bounds/output preservation, initialized
DEFAULT transfer, partial destination obligations, reserve OOM rollback,
exclusive-use rejection, early releases and unknown-completion retirement at the
host native-call seam. `test_windows_texture_regions.c` is a separate official-SDK
CPU call control for actual placed footprints, boxes and source state restoration;
it creates no adapter/device. Authored SDK controls still require actual SDK
compile/execution. Neither host controls nor SDK call composition establish GPU
execution or qualify the public frontend. Public copy/view/sampler orchestration
and its source-bound execution evidence remain in the native owner.

The dynamic sampled/storage composition has focused portable owner controls in
`tests/test_sampled.c`. `tests/test_windows_sampled.c` uses official SDK interfaces
and real root-signature serialization to check descriptor classes, sampler fields,
heap/table indices, alias barriers and second-heap OOM rollback without creating a
device. Authored SDK controls require actual Windows compilation/execution;
portable controls alone do not establish driver support or public execution.

## Typed graphics projection

The additive `doe_dx_shader_compile_projected` accepts a vertex-only typed mask
and preserves the existing `doe_dx_shader_options` layout and legacy compile
entrypoint. The native frontend derives that mask from selected, validated
fragment input locations; the compiler emits the guarded VS carrier. This SDK
adapter passes the compiler's named definition as a bounded uint32 literal and
uses explicit prefix-stable DXC packing on both graphics profiles. It never
rewrites WGSL/HLSL or reclassifies source fields. Zero keeps position only; absent
projection retains the standalone compiler output. Invalid stage/reserved input
leaves the caller's result untouched through the same FP/errno/LastError and
blob/module lifetime owner.

`tests/native_shader_projection.c`, run by `build_shader_windows.cmd`, is an
actual official-DXC CPU control for zero/full/sparse masks together with the exact
entry selector, legacy absence, wrong-stage/reserved/null failures and preserved
host state. Its authored source has not yet run on the SDK; compiler DXIL controls
and later source-bound Windows pipeline/pixel results are separate evidence.

### Guarded indexed and indirect whole-pass source checkpoint

The whole-pass API accepts caller-normalized DRAW/DRAW_INDEXED argument records
and bounded index views. The native bridge compiles the exact shared V2 argument
normalizer and atomic index scanner through the C compiler and selected DXC
provider before entering the device lock. Guard pipelines belong to the existing
graphics pipeline; scratch, parameters, input allocations and command signatures
use existing pipeline/command/context retirement. No CPU mapping or interpretation
of user arguments or index values occurs. Fetch capacities are exact 64-bit layout
bounds; uint16 alignment, negative base vertex, instance bounds and strip restart
are handled by the shared guard contract. Indexed strips carry the matching SDK
strip-cut format. Compatible IA/indirect/shader reads aggregate native transitions;
a writable alias is rejected before pass mutation.

Guard dispatches precede the whole native render pass. A preparation failure
causes the existing submission path to discard the unsubmitted command, including
any earlier guard dispatches; unknown submitted work retains every ordinary lease.
All render descriptor heaps and required command signatures still prepare before
attachment transitions or clears. Empty/direct behavior remains unchanged.

Portable owner tests cover indirect/index view shape, uninitialized arguments,
alignment/range failures and early release. The registered
`check-d3d12-indirect-layout` control compiles both shared guard sources to actual
HLSL and verifies compiler-owned register/stride/access metadata and failure
nonpublication. This newer checkpoint has **not** yet had SDK compilation, DXC
execution, GPU guard execution, indexed pixels or WARP/physical-device validation.
Earlier source-bound SDK receipts do not qualify it.

### Public query source checkpoint

The additive query API extends the existing timestamp heap/use owner to 4096
slots and occlusion heaps. The original 1..64 timestamp constructor, raw-tick
READBACK API and its tests retain their behavior. Upper availability words are
published only by the same observed-completion path; canceled/failed commands
never publish speculative writes. Occlusion begin/end events balance inside the
whole-pass sequence, including zero-draw scopes, and retain the same query owner.

The actual direct queue's timestamp frequency is queried during context creation.
The native adapter advertises timestamp-query only with that fact and SM6 support;
a requested device compiles the unchanged common timestamp kernel before
publication. Query resolve writes available raw results to DEFAULT scratch, keeps
unavailable slots genuinely zero, normalizes timestamps on the GPU, then copies
the exact requested range into the user's buffer. User results never map or use
host timing. The same storage-kernel compiler/reflection owner serves timestamp
normalization and draw guards; there is no second shader engine.

Portable tests cover slot4095, speculative/publication/cancellation bitmap state,
DEFAULT resolution, occlusion event balance and early release. Real C-compiler
HLSL reflection controls include the common timestamp kernel. These changes await
fresh SDK compilation and actual DXC/query execution; they do not inherit earlier
source-bound SDK or hardware qualification.

### Explicit fragment absence and depth-only source checkpoint

Depth-only D32 passes now carry no color target, and vertex-only pipelines carry
explicit `fragment_absent` / `color_absent` flags. An empty fragment description
without the absence flag is still invalid; supplied fragment rows with the flag
are rejected. The native bridge compiles only the selected stages, uses an empty
interstage projection for a missing fragment, and keeps a genuinely absent PS in
the SDK PSO. No synthesized fragment shader is substituted. Fragment-present,
color-absent depth shaders remain distinct from vertex-only pipelines.

Depth-only viewport/scissor bounds use the depth extent. Empty draws and passes
still execute depth clear/load/discard, with the same DSV preparation and ordinary
texture/pipeline leases. Portable tests exercise explicit absence, contradictory
rows, missing-stage rejection, depth-only draws and discard; actual-compiler map
controls cover a single selected vertex stage. SDK PSO controls have new absent-PS
and zero-RTV assertions but have not been executed for this source checkpoint.

### Multisample source composition

The additive sample-count texture constructor admits sample4 attachments after
real format and quality-level queries. Whole-pass PSOs carry sample count, sample
mask and alpha-to-coverage; RTV/DSV/SRV dimensions reflect the immutable sample
contract. Resolve executes before discard with source/destination transitions and
existing command texture leases. Buffer transfers reject MSAA; equal-sample whole
texture copies use the existing whole-resource operation. Native frontend zero
initialization is a real clear pass, not an invented initialization bit.

Portable controls cover sample mismatch, resolve initialization, whole copy and
unknown-completion retention. These controls do not qualify actual SDK linkage,
DXC compilation or D3D12 rendering of this new source. MRT, stencil and broader
subresource/format/view work remains separate.

### MRT, stencil, formats and immutable views: source checkpoint

The whole-pass owner now composes eight target slots (including absent slots),
independent blends, stencil faces/masks/reference, typed depth formats and exact
32-bit integer clears through real UAV descriptors. A shared format table drives
public mappings, block footprints and SDK formats. Immutable texture shape and
view owners cover mip/layer ranges, 1D/2D/3D, array/cube views and depth/stencil
planes. Initialization clears or uploads every actual SDK subresource; it does
not mark uninitialized slices ready. HLSL target metadata explicitly distinguishes
the array-view representation from legacy Texture2D bytecode.

Extended backend region operations have concrete per-subresource SDK copies.
The public native copy frontend still deliberately rejects non-base/multi-image
regions in this checkpoint; selected render attachment views, same-slice scratch
copies, and read-only depth sampling remain integration work. Nonzero immediates
are rejected until real b1 root constants are forwarded. Raw half storage, HWND
presentation, device policy and persistent PSO caching are not completed here.

Internal admission/text-output assertions were extended for these new source
contracts, not substituted for frozen consumer pixel oracles. Portable builds
and controls are not Windows SDK, DXC or physical-GPU qualification.

### Explicit native pipeline library

The cache API uses actual `ID3D12PipelineLibrary` create/load/store/serialize
operations for compute, legacy graphics, mesh and render PSOs. Keys encode every
current SDK descriptor value plus exact DXIL and serialized root-signature
hashes, never pointer values or structure padding. The on-disk envelope binds
adapter LUID/vendor/device/software identity and queried driver version; a shared
SHA256 owner checks the bounded native payload before passing it to the driver.
The input allocation stays alive until the library is released.

Only an explicit directory enables persistence. Its parent must already exist;
configuration may create the final directory. Call flush to persist additions:
teardown does not write files implicitly. Serialization uses an exclusively
created temporary sibling, flushes it, then atomically replaces the prior file.
Incompatible or checksum-invalid inputs are discarded before native use;
documented native-library compatibility failures can initialize an empty
library, but failed GPU pipeline creation is never retried. Non-device-loss
cache-store failure preserves an already valid PSO and increments the observable
addition-failure counter. Device loss enters the existing context retirement
owner. These sources and SDK descriptor controls do not claim Windows driver or
physical-GPU qualification.

### Immediate/raw buffers and selected subresources: next source composition

Compute and graphics roots now bind actual b1/space7 32-bit root constants from
canonical immutable command snapshots. Required-byte masks reject missing values
before recording. Raw half-containing storage uses real R32_TYPELESS RAW SRV/UAV
views, with physical extent rounded4 only within the allocation. Exact logical
binding lengths are uploaded into b2/space7 at canonical resource indices, with
unused slots zero and table extent matching compiler metadata. The upload resource
belongs to the existing descriptor-use retirement list, including unknown fences.
True native16 admission checks queried OPTIONS4 and SM6.2; wave admission checks
actual OPTIONS1/SM6.0. Workgroup wave counters are genuine compiler groupshared
operations, not inferred lane arithmetic.

Native copies and queue writes now forward mips, array/3D images, aspects and
independent image strides. Byte-granular occupied rows share the existing buffer
copy owner; canonical public buffer-copy alignment is unchanged. Same-subresource
disjoint copies use a real temporary texture, fully populated before reading and
retained through the ordinary command lifetime. Overlapping regions still reject.
Selected RTV/DSV/UAV clears and resolves carry mip/layer/3D-slice views. Disjoint MRT
views share one allocation lease and deduplicated transitions; overlapping views
reject. A partial-view clear never initializes untouched subresources. Read-only
depth/stencil sampling uses separate plane states and read-only DSV flags, with
pass-managed depth transitions excluded from per-draw shader transitions.

Portable controls cover exact2-byte logical tails, physical-padding rejection,
immediate snapshots, scratch OOM/unknown-completion leases, selected attachment
bounds and read-only depth aliases. These are source/ownership controls. Actual
Windows SDK compilation, DXC execution and GPU readback remain distinct gates for
this source batch; previous source-bound CI is not inherited. HWND surfaces and
persistent PSO caches are being composed by their respective owners.

### HWND surface owner (source and host contract)

`doe_d3d12_surface.h` composes a real HWND flip-discard DXGI swapchain with the
existing context, texture and submitted-command owners. Acquisition uses
`GetCurrentBackBufferIndex`/`GetBuffer`; ordinary texture leases retain the
swapchain. PRESENT/COPY_DEST transitions use the existing command list. Present
runs after ExecuteCommandLists and before the same queue's fence signal. Neither
a failed Present nor a failed signal releases uncertain work; the first failure
survives recovery. Discard records the return transition without Present. Dropping
an executed acquisition without discard fails the surface closed.

ResizeBuffers requires all frame/view/command leases gone and preserves the old
configuration on admission failure. Changing buffer usage requires replacement;
FIFO and queried tearing-enabled Immediate are the initial modes, opaque HWND
composition only. Host typed-call controls prove ordering, lease retention,
first-error recovery, discard and resize admission, not SDK compilation or a
window/driver run. The public WebGPU frontend composes this owner; actual Windows
SDK compilation, DXGI window, resize and device-loss execution remain separate gates.

Bound sampled/storage views now use immutable mip/layer overlap checks, including
cross-stage aliases. Disjoint SRV/UAV subresources share the same texture lease;
SDK transitions aggregate exact selected subresources, retaining the whole-resource
fast path when every view covers the allocation in one state. Overlapping writable
views still reject. Focused controls exercise distinct layers/mips and overlapping
rejection. Native fragment admission now uses the actual SM6 SV_SampleIndex,
SV_Coverage and SV_Depth implementation, while canonical alpha-to-coverage and
fragment-depth validation remain authoritative.

Vertex draw-offset metadata uses vertex-visible `b3, space7` uint2 root constants.
Direct draws bind first vertex/instance; guarded indexed/indirect draws project
base-vertex bits and first instance using GPU buffer copies, then a pipeline-owned
command signature consumes the constants and unchanged SDK draw record. Signed
base-vertex addition is modulo uint32 in the HLSL wrapper. Portable controls cover
reserved-register/budget rejection and the larger indirect record; SDK and device
execution qualification remain separate.

### Queried optional formats and component views

The native adapter consumes queried BC sliced-3D, BGRA storage, format-tier,
float32 filtering/blending and RG11B10 render capabilities. Required members of
format families are checked separately; RG11B10 rendering includes actual 4x
sample quality. Unsupported query values do not invent capability, and native
query failures preserve their first error. WebGPU feature enablement remains the
native/resource owners' responsibility, separate from backend format mapping.
The retained WGSL storage-format vocabulary remains unchanged; optional resource
and layout admission is not a claim of additional shader syntax.

Immutable sampled views carry typed component selection through native binding
snapshots into real D3D12 SRV Shader4ComponentMapping, including zero and one.
Nonidentity mapping is rejected at storage and attachment use, not merely because
a view was created with those usage bits. Storage allocations require typed
stores; actual read/write UAV bindings additionally query typed-load support.
Explicit write-only bindings therefore do not demand unrelated load support.

Portable owner checks exercise mapping preservation, bad-component rollback and
storage-use rejection. SDK CPU controls cover the real component encoding and
optional format/query distinctions; their authored source is not an SDK run or
a GPU execution result. CoreFeaturesAndLimits is not inferred from feature level
or these optional-family queries alone.

The additive explicit shader requirement501 selects official FXC `cs_5_1` for
compiler-certified compute artifacts. Default/legacy600, native16/620, raw source
imports and graphics keep their DXC paths. The native frontend consumes the
compiler record, never source patterns or zero legacy flags; there is no compiler
retry. FXC uses strictness, warnings-as-errors, IEEE strictness and optimization3,
retains a typed `ID3DBlob` and the exact loaded module, and shares the existing
host-state/diagnostic owner. DXC objects retain their distinct `IDxcBlob` type.
Native Windows staging reuses the selected-SDK FXC packaging owner used by Dawn,
records its exact identity and includes it in relocated packages. `native_fxc.c`
is an explicit SDK CPU control over the certified emitted corpus, exact5.1
reflection, failure outputs, entry defines, host state and blob/module lifetime;
it is separate from physical GPU output and performance evidence.

The gated initialization debug-layer control retains the SDK's READBACK
`UNMAP_RANGE_NOT_EMPTY` advisory: CPU writes are supported, and the constructor
reports their actual full range. It also records `CREATERESOURCE_STATE_IGNORED`
for DEFAULT buffers. These documented advisories are counted explicitly; errors,
corruption and all other warnings fail the control. A pass does not mean the
SDK emitted no warnings.
