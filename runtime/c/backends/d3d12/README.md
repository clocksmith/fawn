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
  transfer ranges and scoped map/unmap. Default/readback initialization requires
  a full copy, published only after successful completion.
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
- Procedural or typed vertex/instance input: up to eight slots, sixteen TEXCOORD
  attributes, explicit 32-bit float/uint/sint scalar/vector formats, checked direct
  spans and bounded uint16/uint32 indexed views. Layouts are copied at creation.
- Timestamp heaps (1..64 slots), explicit EndQuery and ResolveQueryData into
  readback, real direct-queue frequency and completion-backed slot publication.
- Single-use command recording, early caller release, atomic submission ownership,
  finite waits, partial retirement, unknown-completion recovery and terminal loss.
- Typed statuses, first close failure, allocation rollback and native bounds.

The additive [whole-pass render API](include/doe_d3d12_render.h) composes
independent VS/PS layouts with stage-visible root tables. Its two immutable stage
maps preserve 64 resource rows and 16 runtime sampler rows each, packing 128/32 into
one resource heap and one sampler heap per command descriptor use. Compute keeps
its existing 64/16 limits. Module-local resource ordinals never identify another
stage's resource. Compatible read aliases share allocation leases and transitions.

The first public graphics bridge is procedural triangle-list rendering to one
RGBA8/BGRA8 UNORM color target at sample 1, with frontFace, viewport and scissor.
Whole passes execute empty/zero-draw CLEAR/LOAD/STORE. Discard records a real zero
clear at pass end, so later reads observe initialized zeros through the existing
texture-use owner; no frontend availability bit is added. All native heaps/views
prepare before recording. Ordinary command/pending leases retain every dependency
through unknown completion. This is a checkpoint toward retained graphics parity,
not a narrower final acceptance target. Eight MRT slots, broader color/vertex
formats and vertex inputs, depth/stencil, MSAA, blend/cull and GPU-guarded indexed
or indirect draws and public fragment UAV writes remain concrete migration work.

`tests/test_render.c` exercises 128/32 stage capacity, immutable values, empty and
zero draws, preparation failures and early-release/unknown leases at the portable
call seam. `tests/test_windows_render.c` checks real SDK root serialization and
native typed call composition without creating a device. The newer whole-pass
source has not yet received an SDK or driver run; prior static-graphics receipts
remain historical evidence for their exact sources.

All context calls require caller serialization. Objects keep the context busy,
not independently reference-counted: release every caller object, poll pending
submissions, then call context_destroy. BUSY means the context is still owned by
the caller and must remain available for polling/release. Submitted commands are
context-owned even if the caller releases them. Recording-only commands are
canceled by release. Different pending commands may use disjoint resources; a
buffer, texture or timestamp heap cannot belong to another recorded/unresolved command. This restriction is
explicit, not a claim of full command-buffer or WebGPU concurrency parity.

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
is not SDK compilation. Bounded fence polling avoids a timed-out native event
registration retaining an unsafe event handle. Its performance is unqualified.

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
UAVs 64; Tier 3 CBVs are clamped to 64. ALL-visible graphics root tables use the
same conservative combined counts. This is not complete WebGPU graphics admission.

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

## Buffer aliases within one command

Repeated read-only views of one buffer may overlap, including CBV plus structured
SRV views. Disjoint writable UAV ranges may share that buffer too; overlapping
writable ranges and read/write class mixing reject before native recording.
Aliasing between separate unresolved commands is still rejected by the existing
exclusive-use owner. Texture, geometry and indirect-argument alias restrictions
remain unchanged.

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
and query objects retain their existing exclusive command pointer plus a stable
slot index. Lookup is constant time even when storage moves; no caller retains
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
