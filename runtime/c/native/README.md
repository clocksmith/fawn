# Experimental C public WebGPU native frontend

Status: source-built and developer-exercised through pinned `webgpu.h`.
Production/default selection is unchanged. Scoped CPU and selected-device evidence
does not establish independent acceptance, full WebGPU replacement, performance
or a qualified hardware matrix. `src/procedures.c` and the exact export inventory
own implemented symbols; unknown names return NULL from `wgpuGetProcAddress`.

## Backend selection and checked scope

Vulkan, Metal and Windows D3D12 use tagged contexts and payloads within one device,
resource and pending-submission architecture. Undefined selects Vulkan on Linux,
Metal on Apple and D3D12 on Windows for this experimental provider. Unsupported
selections fail without fallback.

The authored Windows compute/buffer path consumes compiler-owned HLSL ABI11,
the official DXC SDK adapter and the existing [private D3D12 owner](../backends/d3d12/README.md).
Device creation reopens exact adapter identity. Native allocations start with real
zero initialization; uniform padding never widens logical storage descriptors.
The D3D12 queue source returns after accepted uploads/submissions, retaining the
existing common pending command/device/resource leases. Distinct resource work
can queue independently; conflicting reuse discards provisional native work,
observes the accepted predecessor through a held event outside the mutex, and
revalidates before recording again. Timeout/cancellation never retires GPU work.
Unmapped-at-creation MapRead buffers use native READBACK storage and map the
completed allocation directly. Mapped-at-creation buffers retain DEFAULT storage
for initial writeback; both paths zero the complete physical allocation before publication.
The authored texture-copy path adds five four-byte color formats in 2D, with one
mip, layer and sample, real zero initialization and partial GPU transfers that
preserve public row padding. Immutable views and sampler descriptors use the
canonical resource owner. Sampled/storage compute bindings and runtime sampler
values now follow compiler-owned resource metadata; their newer SDK/device
execution has separate evidence. Procedural single-color graphics is now authored
with a separate public pixel control; presentation remains uncomposed.
CoreFeaturesAndLimits requires the complete queried format and frontend-limit
envelope described below; it is not inferred from the D3D feature level alone.
The [public D3D12 developer receipt](DEVELOPER_RECEIPT_D3D12_PUBLIC.json) binds an
actual Windows SDK DLL build and four unchanged C consumers to one source, with
the same binaries passing on CPU WARP and the named Miles GPU. It covers compute,
uniforms, binding aliases and command retention. Missing APIs, later source,
Node/browser integration and broader qualification retain their own gates.
Current D3D12 direct compute pipelines compile only their application shader.
The first admitted indirect dispatch prepares one immutable guard on its device.
Concurrent first users may duplicate temporary compilation, but serialized
publication creates one native helper pipeline. Cancellation, device loss and
failed preparation leave no partial helper published; later valid use may retry.
The existing command owner retains the helper until recorded work retires.
`make check-d3d12-guard-prepare` exercises this orchestration without a GPU;
physical indirect readback remains a separate check.

The later indirect path copies GPU arguments to private scratch, executes the
shared compiled bounds guard, and copies sanitized dispatch metadata without
altering caller arguments. The [later public backend receipt](DEVELOPER_RECEIPT_ATOMICS_INDIRECT_TRANSFERS.json)
binds eight unchanged C controls to a newer exact source: actual Windows SDK build,
CPU WARP and the same binaries on Miles. It adds indirect dispatch, all 11 atomic
operations and texture transfers; it retains the separate Node loader link failure.
The [Windows Node receipt](DEVELOPER_RECEIPT_NODE_WINDOWS.json) binds its corrected
LLVM addon build, real Win32 loader controls and unchanged first-kernel result to
CPU WARP and the same provider/addon on Miles. It is an isolated developer
installation, with no npm publication, browser or production-default claim.

Vulkan composes compute, images, graphics, queries and Linux presentation within
the explicit limits and feature checks below. CPU execution receipts prove their
recorded outputs and lifetimes; they do not qualify physical Vulkan performance.

Metal composes shared buffers, compute, initialized color and depth/stencil
textures/views, samplers, color transfers, direct/indexed render passes and
GPU-guarded indirect draws.
Depth/stencil sampled views use canonical aspect admission, combined native depth
formats and X24/X32 stencil views. Real parent-format clears preserve initialized
companion aspects; sampled views and temporary clears retain the same pending
command leases. Depth/stencil copies and storage remain explicitly unsupported.
`tests/metal_depth_cases.h` covers host call identities, discard/reinitialization,
typed failure rollback and early-release/unknown lifetimes. The unchanged
`depth-sampling` consumer owns the independent rendered-depth/stencil readback
oracle. The [shader and sampling receipt](DEVELOPER_RECEIPT_SHADER_SAMPLING.json)
binds its actual M3 readbacks to `63f86dfc4` separately from the earlier `5134cbf06`
compiler/module/override controls on CPU Vulkan, M3, WARP and Miles. Those sources
and consumers do not qualify later changes. `depth-comparison` separately executes
the unchanged compiler WGSL fixture over rendered mipmapped and layered depth,
checking Less/Greater/Equal, sampler addressing, offsets, argument order and
allocation guards; its execution evidence must bind its own source.
Compiler-owned MSL and ABI12
per-entry metadata supply exact names, independent resource namespaces, dense
native slots and runtime size indices. The common Metal binding owner validates
and lowers those records for compute and graphics; public sparse binding numbers
never become native slot arithmetic. WGSL meaning stays in the compiler.
Direct vertex input lowers the pinned scalar/vector integer, normalized, half,
packed and BGRA formats through SDK-bound Metal identities. Public scalar-kind
and offset alignment remain independent of Metal's weaker component alignment.
Host descriptor/rollback controls and SDK checks cover this conversion; per-format
physical fetch has separate evidence. The [M3 vertex receipt](DEVELOPER_RECEIPT_METAL_VERTEX_FORMATS.json)
binds the unchanged consumer's exact draw/readback for 33 selected formats and
admission controls to its source. It does not qualify all 41 admitted formats.

The same later receipt binds both public atomic consumers to actual M3 execution,
including the unchanged retained all-operations WGSL and its exact readback oracle.
It does not establish atomic contention or forward-progress qualification.

Metal exposes composed IndirectFirstInstance, DepthClipControl and
Depth32FloatStencil8 paths in addition to calibrated TimestampQuery. Float32
filtering follows supports32BitFloatFiltering; float32 blending and RG11B10
rendering follow known native family facts, with actual store/resolve support
required for the latter. Disabled features retain shared admission. The retained
draw and format consumers exercise all two draw and four format feature branches
on M3. RG11B10Ufloat core sampling remains independent of optional rendering.
Metal's named format projection also includes the thirteen core snorm/integer/
packed formats exercised by `core-formats`. Initialization asks the resource
validator whether a private attachment clear is legal; other shapes use real
zero staging bounded by native allocation capacity, not public maxBufferSize.
This preserves non-renderable sampled formats without adding public usage rights.
BC and BC sliced-3D follow the native compression capability. Queue writes pack
canonical block rows; buffer transfers keep full block footprints while native
edge extents use logical mip dimensions. Initialization and transfers share the
existing command leases. The unchanged M3 `bc-compression` consumer exercises
edge mips/copies, padded rows/layers, exact block sampling, large zero publication
and sliced 3D. ETC2/EAC and ASTC LDR now compose through native capability
admission, named formats and independent block geometry; ASTC sliced 3D needs
its additional enabled feature. Their 38 public format/copy host controls are
not an extension of the retained physical BC evidence.

Texture component swizzles use the same canonical chain/channel validator on
Vulkan and Metal. Metal reports the feature on Apple2- or Mac2-family devices;
nonidentity views preserve the shared storage-binding/attachment rejection at
use. Fragment storage texture writes now use ordinary reflected stage slots,
image initialization, attachment-alias validation and pending leases. Public
Metal host-ledger tests exercise both additions, including early releases;
physical rendering qualification remains separate.

Metal's workgroup envelope follows the selected device dimensions and known GPU
family total; each pipeline also checks its actual maxTotalThreadsPerThreadgroup.
Default devices retain 256 invocations; requiredLimits can request higher reported
limits. Dispatch remains 65535 groups per axis with conservative 256-byte buffer
offsets. Resolved device limits bound graphics and resource admission. Public
occlusion query sets now use the existing Metal visibility buffer and command
leases, resetting only indices begun in each pass. Empty/unavailable results are
zero and untouched indices survive later passes. CAMetalLayer presentation uses
real drawable textures with retained generations. Depth/stencil transfers and
fixed depth bias/clamp use native Metal operations. Fragment depth/sample builtins
and DepthClipControl compose through canonical admission. Partial fixed sample
masks remain an explicit missing path. Public timestamp
queries use paired native CPU/GPU clock calibration, real ordered counter samples
and shared integer-limb GPU nanosecond conversion. Unavailable results are zero,
native error sentinels survive conversion, and same-recording rewrites preserve
earlier resolve snapshots. Compute indirect dispatch now lowers
through a lazy immutable device guard, GPU-copied private arguments and the actual
Metal indirect-dispatch selector. Host rejection, rollback, sanitizer and lifetime
controls pass. The [M3 indirect receipt](DEVELOPER_RECEIPT_METAL_INDIRECT.json)
binds actual public GPU-generated arguments, bounds suppression, dispatch
metadata, preserved caller bytes and early releases to its exact source.

The [Metal draw-guard receipt](DEVELOPER_RECEIPT_METAL_DRAW_GUARDS.json) separately
binds the unchanged `indexed-render` consumer to `cb6340154`: 192 exact pixels,
nine GPU-produced indirect records, 13 validation controls and early releases on
one M3. The common argument normalizer and GPU index scan use existing command
leases; caller arguments and indices stay unchanged. A companion `6ca18bffa` run
executes 23 guard records, testing 64-bit capacities and simulated large ordinals
with small index windows. It does not establish large-allocation support or
broader graphics qualification.

[Public Apple graphics evidence](DEVELOPER_RECEIPT_METAL_PUBLIC_GRAPHICS.json)
binds offscreen pixels, early releases, discard zeros and uploads on one M3.
The host `metal-bridge` controls separately exercise failure detail, unknown
completion and tagged routing. Neither evidence scope transfers to unexercised
shader/resource combinations. Darwin builds produce dylibs with @rpath identity
and exact exports; absent platform loaders remain explicitly unavailable.

## Actual ownership and execution path

- Public instances alias the accepted C instance, including its future queue.
  Instance capability exports delegate to its conservative empty-feature/zero-limit
  contract, not inferred new feature support. Requests, map, queue, loss, compilation-info and async pipeline callbacks use
  accepted C callback payloads and leases. Device-loss obligations outlive device
  storage without retaining a device cycle; last external release clears the weak
  callback handle before destruction. Explicit destruction completes loss once.
  A device-scoped callback (map, work done, error scope pop, compilation info,
  async pipeline) that the callback owner refuses for lack of memory returns a
  zero future and, unless the operation already reports its own error, reports
  OutOfMemory to the device, so the callback is never lost silently.
- On each backend, creation on a device that is no longer alive (destroy() or loss)
  returns the resource owner's invalid object, never NULL. Getters keep their
  descriptor values; operations on these objects fail silently; maps complete
  with a failure status; async pipelines resolve with an invalid pipeline; and
  compilation info reports no messages. Once a device is lost or destroyed,
  `native_error` reports nothing, because WebGPU generates no errors after loss
  and the loss callback is the one report. `tests/lost_objects.c` covers objects created before and after destroy().
- Adapters/devices have their sole native references here. Queue handles retain
  their device. Public buffers, shaders, layouts, groups, pipelines, encoders,
  passes and command buffers alias their typed C resource owners, not wrappers
  with duplicate references. Device-bound shader resources adopt the separately
  shareable immutable `doe_shader` artifact.
- Shader creation validates and retains the immutable WGSL module. Pipeline
  creation selects the declared stage and specializes its constants through the
  same compiler for the selected backend. The temporary executable has one entry
  at index zero, its declared name and its static resource closure; it is released
  outside owner locks after native creation. Layout checks include reflected
  minimum binding sizes, access kind, stage, resource/device identity and native
  limits. The Vulkan capability scanner admits Shader/Matrix, baseline 1D images and
  ImageQuery explicitly. The native context opts into demote when actually available;
  its exact SPIR-V extension/capability is admitted only when successfully enabled,
  never from an API-version guess. Other capabilities/extensions reject before pipeline
  creation. SampledCubeArray (45) is admitted only when the device enabled
  imageCubeArray; CubeArray views and automatic/explicit sampled layouts follow
  the same gate, so devices without it reject cube arrays visibly rather than
  claiming core cube-array support. StorageImageExtendedFormats (rg32 storage) is
  admitted only after the device's shaderStorageImageExtendedFormats is enabled. Workgroup
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
  clears before any queue-ordered access, including images without public CopyDst
  usage (owner authorized 2026-10-24: async submit A+B, D2, fence-wait completion; futures later; reconfirmed after the section 7 risk review). On Vulkan, texture creation records the initialization into one
  device-owned batch that the next queue submission or compute-program replay
  submits ahead of its own work, without waiting; every access to texture
  contents is such a submission. The batch is also submitted once it holds 64
  textures and at any texture destroy(), since it retains each texture and its
  staging until it runs; destroy() then frees the image and memory when the
  texture's last command use retires, while views keep only their own native
  views. A failed batch recording poisons every texture
  it held, and final release discards a batch that was never submitted. Borrowed
  surface frames keep their own waited initialization before publication. The
  batch and other private initialization use the same pending-submission owner
  and an explicit submission device lease. Packed queue writes and 256-byte-aligned public command
  copies retain their distinct canonical validation. Native views/samplers remain
  alive through the recorded resource and backend obligations.
- On Vulkan, QueueSubmit returns once `vkQueueSubmit` accepts the work (owner authorized 2026-10-24: async submit A+B, D2, fence-wait completion; futures later; reconfirmed after the section 7 risk review).
  The record stays on the pending list, oldest first, with its native and
  resource leases until it is proven terminal. The progress worker waits for the
  oldest accepted fence outside the device mutex under a command hold, skipping a
  refused submission whose fence never signals; routine settlement (submit,
  texture creation) polls, and running work is never reported as a failure. The
  first terminal failure is reported once to a later public call. Buffer writes
  and maps wait, oldest first, only while a submission still uses that buffer;
  work done waits each pending record's own fence (queue idle only for a refused
  submission) before admitting its callback, so map and work-done callbacks keep
  request order. Metal, D3D12,
  compute programs, private initialization and presentation keep their
  synchronous wait. Uncertain completion retains both native and resource
  obligations; if still unresolved, work-done publishes no future rather than
  reporting success. Releasing all caller handles does not free unresolved work.
  This is not a latency or parity claim.
- TimestampQuery is opt-in at device creation. Generated backend WGSL is compiled
  by the same C compiler before a feature-enabled device is published. Query
  sets alias the resource owner; zero-count sets need no native pool. Query resolve
  records raw timestamps, exact calibrated GPU nanosecond conversion and the copy
  into the caller's destination before later commands. There is no production CPU
  postprocessing or raw-tick fallback. A terminal failed submission discards the
  possibly poisoned normalizer; later work creates a fresh one. Unknown completion
  retains its command-owned dependencies and prevents reuse.
- Genuine compiler-owned vertex/fragment entry reflection drives stage-specific selection,
  shared automatic binding layouts and interface admission. Color (up to the
  device's eight indexed, possibly sparse, slots) or depth-only passes, direct draws, reusable bundles and occlusion queries lower through one
  ordered Vulkan pass span. The resource owner retains pass history, bindings,
  query identity and vertex ranges; native conversion owns no parallel recorder.
  Dynamic viewport/scissor/blend and culling use real backend state. Integer clear
  colors truncate and range-check before a defined C cast; ignored Load clears
  are not converted. Extra fragment outputs are ignored, while written targets,
  scalar/channel compatibility and source-alpha blending are checked.
- Depth16Unorm, Depth32Float, Depth24Plus, Stencil8 and Depth24PlusStencil8 support
  attachment and/or CopySrc/CopyDst usage with actual depth tests/writes, stencil
  operations and read-only bundles. Texel copies follow the resource owner's
  per-format/aspect rules: depth32float depth is a copy source only, depth16unorm
  depth and every stencil aspect are source and destination, and depth24plus
  depth (including the depth24plus-stencil8 depth aspect) never reaches a buffer.
  Buffer copies and writeTexture select one aspect, copy the full subresource and
  use 4-byte offsets (also for writeTexture, stricter than WebGPU's unaligned
  writeTexture offset); texture-to-texture copies use all aspects. Linear bytes
  are 4 (depth32), 2 (depth16) and 1 (stencil). Copy-only depth images are zeroed
  per aspect by real uploads; attachments by real clears. Depth storage is
  rejected. TextureBinding is admitted: texture_depth_2d/_2d_array/_cube reflect
  the default 'depth' sample type, depth aspects also bind to explicit
  'unfilterable-float' layouts, StencilOnly views sample as 'uint', and
  DepthOnly/StencilOnly views of combined formats select one native aspect.
  An all-aspect view of a combined format cannot be bound as a texture.
  Sampled depth images may be cube-compatible. Comparison samplers bind only to
  'comparison' entries. textureLoad, dimension queries, depth textureSampleLevel
  and textureSampleCompareLevel execute through compiler-owned lowering. Explicit
  depth LOD is integer; explicit comparison uses mip zero. Fragment textureSample
  and textureSampleCompare derive their LOD under the compiler's transitive stage
  and uniform-control rules; see the
  [compiler texture contract](../compiler/TEXTURES.md#implicit-fragment-sampling).
  Depth24Plus readback values come from its D32 backing.
  Plus formats use D32/D32S8 backing, not an implied optional public feature;
  depth32float-stencil8 maps to D32S8 only on a device that enabled it.
  The backend's sole per-aspect initialization owner clears undefined contents;
  depth/stencil Discard followed by Load reinitializes only missing aspects on GPU.
  Initialized companion aspects survive. Partial combined-aspect views remain
  unsupported; same-format standalone DepthOnly/StencilOnly views are admitted.
- Sample counts 1 and 4 use actual attachment storage, resolve, sample masks and
  alpha-to-coverage. A vertex-only depth pipeline needs neither a fabricated
  fragment shader nor a color target. Discarded multisample color is initialized
  by the existing backend aspect owner before a later Load.
- Multisampled textures may add TextureBinding to RenderAttachment. The compiler's
  texture_multisampled_2d textureLoad(coord, sampleIndex) bounds-checks the index
  against OpImageQuerySamples (out of range loads zero); automatic layouts reflect
  `multisampled` with 'unfilterable-float' for f32 and 'depth' for depth, and the
  resource owner rejects multisampled 'float' layouts and single-sample views.
  A multisampled binding never pairs with a sampler, so it stays outside the
  whole-pipeline unfilterable/filtering check. CopySrc/CopyDst are admitted on
  multisampled textures: texture copies need equal sample counts and the whole
  subresource; buffer copies need one sample.
- Texel copy layouts take WGPU_COPY_STRIDE_UNDEFINED as WebGPU's undefined:
  rowsPerImage for a one-image copy, bytesPerRow for a one-row copy
  (`tests/copy_layouts.c`).
- Encoding errors follow WebGPU: a rejected pass descriptor, pass command, copy,
  clear, query or debug command only invalidates its encoder, and Finish reports
  exactly one validation error; later valid passes on that encoder add nothing.
  A rejected BeginRenderPass/BeginComputePass still returns an invalid pass that
  records nothing and locks an open parent. Only a command on an encoder or pass
  that already ended (Finish always ends an encoder and unlocks it; a chained
  Finish descriptor too) is reported at the call, and it leaves a valid parent
  valid; End also reports at the call when its parent is not locked (after
  Finish, or after another pass's End). A failed Finish returns a WebGPU invalid
  command buffer whose submit reports again
  (`native_encoding_error`; `tests/encoder_errors.c` counts uncaptured errors).
  Render bundle encoders follow the same rule: their Finish reports one error
  and returns an invalid bundle, and executing an invalid bundle invalidates the
  pass's command encoder, whose Finish reports it. An invalid bundle encoder
  descriptor reports at creation and returns an invalid encoder whose commands
  add nothing and whose Finish reports again. A queue submit that fails
  validation or in the backend consumes every command buffer it names
  (single use), and submission validity is decided before native recording.
- Color storeOp Discard is admitted for single-sample attachments too: Vulkan
  STORE_OP_DONT_CARE and the backend's per-aspect owner marks the subresource
  undefined. Later Load passes, buffer/texture copies and sampled or storage
  bindings first run `doe_vk_commands_initialize_texture`, the whole-image form of
  the same attachment initializer, so they read GPU zero clears (this also covers
  copies/bindings after a depth or stencil Discard). No CPU contents are written.
- Indirect Vulkan commands consume GPU-produced argument bytes through backend-
  generated WGSL guards compiled by the existing C shader owner. Dispatch guards
  use the admitted device dimension limits; draw guards use exact bound vertex/
  instance spans and suppress nonzero firstInstance unless the device enabled
  indirect-first-instance, which then bounds firstInstance+instanceCount instead.
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
  counts, instance-step overrun or (without indirect-first-instance) nonzero
  firstInstance becomes a native no-op.
  Uint32 indices require the enabled native fullDrawIndexUint32 feature. Index
  values are never read on the host; vertex-step fetches through arbitrary GPU
  indices rely on enabled robustBufferAccess. Metal instead uses the shared
  argument normalizer plus GPU index scans, including signed base vertices,
  strip restart and initialized physical padding for odd uint16 ends, without
  widening logical index ranges or reading indices on the host.
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

- Each published device owns one progress worker and an ordinary device lease.
  It polls the existing pending list without waiting for queue idle. Unknown
  completion retains every submission/resource lease even after all public
  handles disappear; terminal success or failure retires them. The first retired
  failure remains observable by a later public operation. An idle last release
  joins the worker before returning. Startup failure publishes no device.
- Background execution retains provider code until process exit, independently of
  GPU/device resources. POSIX keeps one loader reference per non-main image;
  Win32 uses `InitOnce` and `GetModuleHandleEx(FROM_ADDRESS|PIN)` with no ordinary
  `LoadLibrary` fallback. Both admit the worker before publication, outside
  device/resource locks; caller `errno` and Win32 `GetLastError` are preserved.
  Distinct pinned images retain their code until exit, including after the caller
  closes its loader handle. A statically linked main image already has this lifetime.
- Private host primitives supply recursive device locks, ordinary surface/loss
  locks, monotonic conditions and real thread handles. Condition waits hold the
  device mutex at depth one. Thread creation publishes its control under the mutex
  taken by worker entry. Idle release joins outside that lock; worker self-detach
  consumes its real stored handle before releasing its existing device lease.
- `make check-provider-pin` exercises real unload versus retained worker code on
  Linux/macOS without a GPU. The Windows host recipe builds the same real DLL
  unload/pinned-worker control and a PIN-rejection control. Their
  [actual Win32 developer receipt](../platform/DEVELOPER_RECEIPT_WINDOWS_HOST.json)
  closes the bounded host/loader prerequisite at its recorded source. It does not
  qualify the public native DLL, D3D12 execution or production/default promotion.
  Standalone compiler DLL evidence is separate.
  Linux `pending-progress` uses actual CPU Vulkan work
  with completion obscured at backend boundaries: release-all success/failure,
  no premature resource destruction, failed worker startup and synchronous idle
  release. Neither control establishes physical failure recovery or driver liveness.

## Surfaces and presentation (Vulkan, Linux)

`src/surfaces.c` and `src/surface_present.c` implement `wgpuInstanceCreateSurface`,
surface AddRef/Release/SetLabel, GetCapabilities/CapabilitiesFreeMembers,
Configure/Unconfigure, GetCurrentTexture and Present over the Vulkan surface
owner (`backends/vulkan/include/doe_vk_surface.h`). Design decisions:

- Sources: exactly one chained `WGPUSurfaceSourceWaylandSurface`,
  `WGPUSurfaceSourceXCBWindow` or `WGPUSurfaceSourceXlibWindow` with non-null
  handles; any other chain, a second chain node or a non-Linux build returns NULL.
  Platform handles are borrowed and must outlive the surface and its frames. The
  instance-level surface stores only those handles and its label (own label store);
  it does not change the accepted C instance.
- WSI enablement: `compatibleSurface` in `wgpuInstanceRequestAdapter` opens the
  adapter's Vulkan context with exactly that surface's platform bit and the
  backend's complete WSI/maintenance1 chain, then creates and releases a probe
  native surface to prove the selected queue can present to that window. Missing
  support is `Unavailable`, never a fallback adapter or headless image. A default
  request (no compatibleSurface) enables all three Linux platforms with the same
  chain when the loader and device provide them, and opens without WSI when they
  do not; a non-CPU device without WSI still precedes the CPU fallback. webgpu.h
  lets any adapter that can present configure a surface; compatibleSurface only
  narrows selection. Devices inherit the adapter's options, so their contexts
  carry the same WSI chain. `GetCapabilities` returns `WGPUStatus_Error` for an
  adapter opened without that platform; `Configure` with such a device is a
  Validation error. Metal adapters reject any compatibleSurface.
- The native `doe_vk_surface` is created at Configure on the configured device's
  own context (adapter contexts only host short-lived capability probes, serialized
  by an adapter mutex). An adapter's Vulkan context is adapter-only (instance and
  physical device, no `VkDevice`); a default request selects the first non-CPU
  device, else the CPU device, from that one context, and retries with the CPU
  device only when a non-CPU device fails admission. Invalid configuration leaves
  the previous configuration and current frame intact. A successful replacement
  retires the previous generation on the existing pending owner.
- Capabilities advertise RenderAttachment|CopySrc|CopyDst and TextureBinding when
  the native surface supports sampled images,
  the SRGB-nonlinear BGRA8/RGBA8 (sRGB or unorm) formats the surface reports, the
  native present modes (Fifo always required) and alpha modes with Inherit first,
  so Auto aliases the first element and prefers Inherit as the pinned header says.
  Usage always adds internal native COLOR_ATTACHMENT for frame initialization.
  View-format reinterpretation and other usages reject explicitly.
- GetCurrentTexture acquires through the backend's fence-bounded acquisition
  (`NATIVE_SURFACE_TIMEOUT_NS`, 1 s per native wait). Before publication the frame is
  zero-initialized by the canonical attachment initializer in a private submission
  on the existing pending owner, exactly like other published textures. The public
  texture aliases the resource owner with a `native_surface_frame` payload whose
  first member is the ordinary `native_texture`; the payload holds one surface lease.
  A second call before Present returns the same frame with a new reference.
  Status mapping: OK -> SuccessOptimal/SuccessSuboptimal, TIMEOUT -> Timeout (the
  backend retains the pending acquisition and the next call waits that same one),
  SURFACE_OUTDATED -> Outdated, SURFACE_LOST -> Lost (both sticky until
  reconfiguration, also when observed at present), DEVICE_LOST -> Error (loss is
  reported by the loss owner), BUSY (unresolved frame recording) ->
  Error plus a Validation error, everything else -> Error plus its typed error.
  Unconfigured surfaces return Error with a NULL texture.
- Present creates one private command object, attaches the frame with the
  backend's `doe_vk_surface_prepare_present`, and submits it through the same
  `native_pending` owner (`native_submit_present` in `src/queue.c`): submit, enqueue
  `vkQueuePresentKHR` waiting the render-finished semaphore, wait the command fence,
  retire the frame through its presentation fence (or a maintenance1 discard when
  present was never enqueued), then settle. Presentation never retires from command
  completion or queue idle. This is one extra command buffer per frame carrying the
  zero-if-undefined and PRESENT_SRC transition, not a second submission engine;
  the caller's render work keeps its ordinary synchronous QueueSubmit path. The
  public frame is destroyed after Present, so later recording rejects it.
- One acquired frame is current. Presented texture/view handles may survive
  subsequent acquisitions and reconfiguration, but remain invalid for new work.
  Each native swapchain generation retains its stale frames until their leases
  drop; generations on the same device share the platform VkSurfaceKHR owner.
  Unconfigure and final surface release retire through the existing pending owner.
  Old generations never clear a newer public configuration during cleanup. A presentation-fence wait that times out keeps
  the present commands on the pending list: queue settlement then reports uncertain
  completion (as for any unresolved work) until a later surface call retires the
  frame. Unknown presentation completion at final
  teardown retains the native surface, device and platform obligations rather than
  freeing them. Foreign error callbacks run only after the surface and device
  mutexes are released; the surface mutex is always taken before a device mutex.

`make -C runtime/c/native BUILD=... check-present` builds `tests/surface_present.c`
(XCB) and `tests/surface_platforms.c` (Xlib and a role-less Wayland `wl_surface`).
They are not part of headless `check`; each exits 77 (skip, never a pass) when no
display is reachable. Both use `tests/adapter_class.h`. Developer runs on Jupiter
(GNOME Wayland, Xwayland `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0`):

```
DISPLAY=:0 XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.* WAYLAND_DISPLAY=wayland-0 \
  VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  make -C runtime/c/native BUILD=/tmp/doe-c-native check-present
PASS public surface presentation: XCB window 80x40 after resize (SuccessSuboptimal), compatibleSurface adapter, capabilities, 8 verified frames with zero-init/asymmetric pixels/same-submission readback before present, stale-frame and lease rejection, reconfigure, deferred unconfigure, early releases and configure/acquire/present negatives; 1 retried Timeout statuses (CPU Vulkan only)
PASS public surface sources: Xlib window presented; Wayland role-less wl_surface presented to the compositor (not displayed); compatibleSurface adapter, capabilities, configure, exact pixels read back in the presenting submission; 0 retried Timeout statuses (CPU Vulkan only)
```

The same binaries with `DOE_C_TEST_ADAPTER=physical` and `VK_ICD_FILENAMES` unset
passed on Radeon 8060S Graphics (RADV STRIX_HALO, 0x1002:0x1586); that is developer
evidence on that adapter/driver only, not qualification. A sanitizer build
(`SANITIZE='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'`, leak
detection enabled) passed both on lavapipe. Pixels are verified by readback of the
presented image in the presenting submission; what the compositor displayed was not
observed. The Wayland surface has no xdg-shell role and is never shown. The single
Timeout is the FIFO swapchain throttled by Xwayland while it is not repainting the
window; the retry completes the retained acquisition. Resize produced
SuccessSuboptimal on both drivers; explicit reconfiguration then rendered at the new
size. Outdated/Lost mapping is host-tested by the backend owner, not observed here.

`check-present` also runs `tests/surface_faults.c` (XCB, override-redirect
windows): configure, getCurrentTexture, present and unconfigure, with a resize
and the window destroyed between acquire and present, then `wgpuDeviceDestroy`
between acquire and present. Every C-owner host allocation (GNU `--wrap`) and
every result-returning WSI/queue Vulkan call through the device table fails in
turn; each frame-changing failure must be reported. Injected loss first lets
the healthy driver finish its accepted work and presents, so the driver state
matches a real loss. Frames after window loss are counted apart, since whether the X server
still accepts them varies. A concurrency phase presents with resizes beside two
compute threads on one device and a thread running its own sessions.
`check-present-sanitizers` runs it under ASan+UBSan (leaks) and TSan.
Unconfigure and release wait for accepted work still using the frame, so the
native swapchain never outlives a window the caller destroys next; only
unknown completion stays armed for the progress worker.

## Optional device features

`src/features.c` is the one owner of optional WebGPU device features. The adapter
reports a feature only from what the selected Vulkan device really offers: the
canonical timestamp normalizer for `timestamp-query`; linear filtering of
r32/rg32/rgba32 float for `float32-filterable`; blending of those formats for
`float32-blendable`; D32S8 depth/stencil attachment, sampling and transfer for
`depth32float-stencil8`; enabled native drawIndirectFirstInstance for
`indirect-first-instance` and depthClamp for `depth-clip-control`; enabled
color attachment, blending and 4x samples of rg11b10ufloat for
`rg11b10ufloat-renderable`; enabled
textureCompressionBC with sampled, filterable, copyable formats for all
fourteen BC formats for `texture-compression-bc`, plus 3D images of each for
`texture-compression-bc-sliced-3d`; and `texture-component-swizzle` on every
Vulkan device (VkComponentMapping is core). A device
request naming an unreported feature fails.
The device keeps the requested set as `NATIVE_FEATURE` bits and every gated use
checks that set, never the adapter facts: feature formats map only when enabled
(textures, views, pipeline targets, bundles and layouts all reject
depth32float-stencil8 otherwise, and rg11b10ufloat render attachments,
pipeline targets and bundle formats reject without rg11b10ufloat-renderable while
the core format still samples); blending an r32/rg32/rgba32 float target rejects
without float32-blendable; and without float32-filterable those views keep the
'unfilterable-float' sample type, so a 'float' (sampler-using) layout rejects them.
Indirect draws admit a GPU-produced nonzero firstInstance only with
indirect-first-instance (otherwise the guard keeps them no-ops; direct draws
always honor firstInstance), and unclippedDepth rejects without
depth-clip-control; with it the pipeline clamps depth instead of clipping.

`tests/format_features.c` reads back exact results on a device that enabled every
offered feature: linear midpoints of r32float/rgba32float values that no 16-bit
format holds (1000001.0), two additive draws onto a 1000000.0 r32float clear
(1000007.0), depth 0.25/stencil 7 per aspect of depth32float-stencil8, and
rg11b10ufloat packed bits after an additive draw and a 4x resolve. On a
device without them it checks each rejection and that an unoffered feature
fails the request. `tests/draw_features.c` reads back exact r32uint values
a*1000+instance_index for GPU firstInstance 2/1 through indirect, indexed indirect
and bundle draws (an instance-buffer overrun stays a no-op; without the feature
all stay no-ops while a direct firstInstance draws), and unclippedDepth geometry at
z 1.5/-0.5 stored as depth 1.0/0.0 where the clipped control draws nothing. A
feature the adapter does not offer prints SKIP with its reason. BC formats map
only with texture-compression-bc, 3D BC textures also need the sliced-3D feature,
compressed textures have whole blocks at mip 0 (resource owner) and are never
attachments or storage. writeTexture validates with the resource owner's QUEUE
mode (whole blocks and undefined-stride rules, without the 256-byte bytesPerRow
alignment or any block alignment of the data offset and bytesPerRow) and repacks
the touched block rows into tight staging before the native copy; BC1/BC2/BC3/BC7 accept their srgb pair as a view format; zero
initialization stages mip 0's blocks, not texels. `tests/bc_compression.c` decodes solid BC1, BC1-srgb and
BC4 blocks to exact texels through queue writes and buffer copies, copies a 6x6
mip by its physical 8x8 extent, returns exact blocks from texture and buffer
copies, copies a block that reaches only one side's edge in both directions,
writes whole blocks into a 2x2 mip (a 2x2 write rejects), reads one block through
unorm and srgb views, zero-initializes an 8192x8192x8 BC1 texture, and decodes a
3D BC1 texture uploaded with rowsPerImage 2 per slice.
A WGPUTextureComponentSwizzleDescriptor on a view maps each channel (Undefined is
the channel's own); a non-identity swizzle needs texture-component-swizzle.
Such a view rejects when used as a storage binding or attachment, not merely
because its declared usage includes those bits. `tests/component_swizzle.c`
reads (b,a,0,r) and (1,g,r,g) views through textureLoad and nearest sampling as
exact rearrangements of the identity view.

## WGSL language features and texture binding view dimension

`src/features.c` owns the reported WGSL language features
(`wgpuInstanceGetWGSLLanguageFeatures`, `wgpuInstanceHasWGSLLanguageFeature`,
`wgpuSupportedWGSLLanguageFeaturesFreeMembers`) as one switch beside the
instance capability exports; the accepted instance owner is unchanged. A feature
is reported only when `tests/wgsl_language_features.c` shows its probe compiles
through the public shader-module path and creates a real Vulkan compute pipeline
both without and with `requires <name>;`. Every probe also has a control shader of
the same shape without the feature that must create a pipeline, so a rejection is
attributable to the feature. Each unreported probe must be rejected at the module
or pipeline boundary, so the table cannot silently drift from the compiler. The current report includes uniform-buffer standard layout, packed integer dot,
unrestricted pointer parameters, pointer-composite access, texture/sampler lets
and immediate address space. The same semantic and feature owners govern native
admission; standalone compiler defaults remain conservative.

The table below preserves the **initial baseline**, not today's support list.
Current pointer/immediate and stage-extension receipts appear in the later
migration sections. All cited execution here is software Vulkan, not physical
qualification:

| Feature | Without `requires` | With `requires` | First diagnostic |
| --- | --- | --- | --- |
| readonly_and_readwrite_storage_textures | pipeline created | module rejected | unsupported enable or language extension |
| packed_4x8_integer_dot_product | module rejected | module rejected | unsupported type name |
| unrestricted_pointer_parameters | module rejected | module rejected | only function-address pointer types are currently admitted |
| pointer_composite_access | module rejected | module rejected | array value has no named member |
| uniform_buffer_standard_layout | pipeline created | pipeline created | none |
| subgroup_id | pipeline rejected | module rejected | compute shader entry/capability admission failed |
| texture_and_sampler_let | module rejected | module rejected | local variable type must be concrete and constructible |
| subgroup_uniformity | pipeline rejected | pipeline rejected | compute shader entry/capability admission failed |
| texture_formats_tier1 | module rejected | module rejected | unsupported storage texture format |
| linear_indexing | module rejected | module rejected | unsupported compute input builtin |
| immediate_address_space | module rejected | module rejected | unsupported module variable address space |
| buffer_view | module rejected | module rejected | expected ASCII WGSL identifier |

The compiler's `requires` directive admits standard uniform layout when the native
semantic policy selects it, as well as `subgroup_uniformity`. No device here
admits `enable subgroups`, so the subgroup features cannot create pipelines.
Read-only/read-write storage textures already compile and create pipelines; that
feature becomes reportable once the compiler admits its `requires` directive (and an
executed read/write image check is added). The `buffer_view` probe uses the
proposal's `buffer<N>`/`bufferView` spelling; it is rejected at parsing.
An empty list owns no storage (`features` is NULL); NULL instance or output is safe.

`tests/uniform_standard.c` executes compact uniform arrays and nested attributed
structs, aggregate copies and narrow matrices through the public C path with
exact 4/8/12/32/64-byte bindings. Dynamic negative, first-invalid and high unsigned
indices return zero; the final scalar lane of a 12-byte binding is read exactly.
Well-typed wrong-index and wrong-layout variants produce distinct exact outputs.
Both plain source and `requires uniform_buffer_standard_layout;` execute;
`enable uniform_buffer_standard_layout;` and a mixed unsupported requirement are
rejected. Raw compiler lowering uses baseline backend layouts and needs no Vulkan
uniformBufferStandardLayout device feature. These focused software controls do
not transfer physical qualification to this policy change.
The separate [Apple uniform/alias receipt](DEVELOPER_RECEIPT_METAL_UNIFORM_ALIASES.json)
binds actual execution of this consumer and shared-allocation binding controls
to one Metal device and exact source. It preserves the first guardian failure
and the corrected guarded results; broader qualification remains separate.

`wgpuTextureGetTextureBindingViewDimension` follows core-mode semantics. The pinned
header requires accepting a `WGPUTextureBindingViewDimension` chain on
`WGPUTextureDescriptor` and ignores its contents on core devices. This provider has
no compatibility-mode view restrictions, so it accepts exactly that one chain node,
ignores the value, and the getter returns `WGPUTextureViewDimension_Undefined` for
every texture, as the reference core implementation does. Unknown or repeated chain
nodes still reject. Every Vulkan adapter lists `core-features-and-limits`
(there is no compatibility mode), and every device has it enabled whether or
not `requiredFeatures` names it.

## Limits

`src/limits.c` owns the WebGPU limits. Adapter limits are Vulkan facts
intersected with what this frontend enforces and executes: per-stage binding
counts the pipeline binding storage holds for two stages
(`NATIVE_MAX_PIPELINE_BINDINGS`, also the Vulkan backend's per-pipeline binding
capacity), which on Vulkan is 10 storage buffers and 8 storage textures with
sampled textures, samplers and uniform buffers at their defaults; 32 vertex
attributes from the device (the backend's attribute storage) with vertex buffers
at eight (resource-owner draw packets); maxBindingsPerBindGroup 1000 (sparse
resource-owner storage), dynamic uniform and dynamic storage buffers at 16
each capped by that stage's uniform/storage buffer limit, so a layout at the
limit fits one stage (applied by the resource owner, 32 per group), maxBufferSize from Vulkan
maxMemoryAllocationSize with binding sizes bounded by it and rounded down to 4,
the minimum dispatch dimension across axes, and workgroup sizes/invocations at
the C compiler's 256 bound. A device resolves requestDevice requiredLimits per
the specification: an undefined member takes the default, a value better than
the adapter's (or a non-power-of-two alignment) fails creation, and each device
limit is the better of the required value and the default. An adapter below a
default keeps its own value. Every frontend check (texture and surface sizes,
buffer size, binding sizes and alignments, binding slot limits, binding numbers,
bind group count, vertex buffers/attributes/stride, inter-stage variables,
workgroup size/storage, dispatch counts) reads the device limits, and
`wgpuDeviceGetLimits` reports them. Internal buffers are not WebGPU buffers:
queue writeTexture stages only the touched bytes in a buffer bounded by Vulkan
maxMemoryAllocationSize, never by maxBufferSize (`tests/large_write.c` writes
past the 256 MiB default and checks that a public buffer of that size still
fails). `tests/limits.c` prints the default,
adapter and device columns and checks each resolution rule and default-limit
rejection, and on a device that requires the adapter limits executes 10
storage buffers, 8 storage textures, 32 vertex attributes and eight rgba32float
targets with exact readback, each refused on the default device. Workgroup
dimensions and invocation limits now follow Vulkan/D3D12 device facts and
Metal's queried family envelope plus per-axis device properties; every Metal
pipeline still checks its actual maxTotalThreadsPerThreadgroup. The default
device keeps 256 invocations unless requiredLimits raises it.
`tests/workgroups.c` checks default rejection, raised limits and exact execution
at 256/512/1024 invocations when advertised. Bind groups (groups 0..3) and binding
numbers (0..999) stay at the C compiler's bounds; maxImmediateSize stays 64 (no
compiler immediates). The native shader policy selects core binding numbers
(`native_shader_semantic_flags`), so shaders reference bindings 0..999 in groups
0..3; the compiler's standalone default stays 0..31.

## Storage textures

The resource owner's `resource_storage_format` table is the WebGPU core storage
format set: rgba8unorm/snorm/uint/sint, rgba16uint/sint/float, r32uint/sint/float,
rg32uint/sint/float and rgba32uint/sint/float. All admit write-only and read-only
access; read-write is limited to the three r32 formats. Layouts with other formats
or read-write on other formats are Validation errors, as is StorageBinding usage on
a non-storage format; storage bindings need StorageBinding usage, one mip and the
exact format. bgra8unorm storage (bgra8unorm-storage) is not offered: the C compiler
has no bgra8unorm storage format. The compiler's sixteen storage formats map to
these names; 1D, 2D, 2D-array and 3D storage views are admitted. A `read-write`
layout entry admits a shader declaring `read_write` or `write` access; `write-only`
and `read-only` entries require the same shader access (validating shader binding).
Fragment-stage writes (write-only/read-write) use the device's enabled
fragmentStoresAndAtomics; a Vulkan adapter without that feature is refused at
requestAdapter (UNAVAILABLE), because WebGPU core admits fragment storage writes
under the per-stage limits and no limit can report their absence. Vertex-stage
writes stay rejected.

Default layouts use the compiler's ABI16 selected-entry sampling pairs, including
transitive helper-formal substitution. Only f32 textures actually paired with a
sampler become `float`; unrelated textureLoad-only textures remain
`unfilterable-float`. Explicit layouts reject Filtering samplers with
UnfilterableFloat textures only when the shader pairs those exact resources.
The public texture-binding test exercises an unrelated r32float load alongside
a nested-helper linear sample and verifies exact readback, plus explicit valid,
invalid and nonfiltering pair layouts. Metal host controls share the layout test;
physical GPU qualification remains separate.

`tests/storage_textures.c` writes all sixteen formats with textureStore and checks
every channel of a 4x4 readback exactly; reads r32uint/sint/float through read-only
storage and increments them in place through read-write storage; writes 1D,
2D-array and 3D views; stores from a fragment shader at each pixel; and repeats
write-then-sample across passes in two submissions; writes all 16 texels of an
r32float texture through a write shader bound by an explicit read-write layout and
reads them back exactly. Negatives cover three layout/shader access mismatches, StorageBinding
on rgba8unorm-srgb, read-write rgba8unorm in an explicit layout and in a shader,
sampling a storage-only texture and storage-binding a sampled-only texture.

The `packages/doe-gpu` storage-texture visibility integration test fails on this
provider because the test itself violates WebGPU: it names a WGSL variable
`target` (a reserved word) and binds an r32float view to a `float` (filterable)
sample-type layout, which needs `float32-filterable`. With `destination` and
`unfilterable-float`, the same test passes on this provider.

## Multiple color attachments and sampler anisotropy

The resource owner admits indexed color slots (see its README); native conversion
keeps WebGPU null slots (`view == NULL`, Undefined target format) as unused slots,
never packing indices. Pipelines carry per-target format, write mask and blend
state; each used target with a nonzero write mask needs the fragment output at its
slot index with the format's scalar kind and enough components (four for source-
alpha blending); alpha-to-coverage needs target 0. Passes carry per-slot load/store/
clear values and optional resolve targets; bundles carry sparse color formats.
`maxColorAttachments` reports the device's real count bounded by eight, and
`maxColorAttachmentBytesPerSample` reports 16 bytes (the largest WebGPU
render-target cost) per admitted slot, 128 for eight slots, since Vulkan has no
per-sample byte limit; both feed resource admission. All slots lower into the
backend's one native pass span. The C compiler already emits and reflects multiple
`@location` fragment outputs (struct outputs; spirv-val clean for Vulkan 1.2).

Sampler `maxAnisotropy` has no upper bound at admission; values above one require
all filters linear, use the device's enabled `samplerAnisotropy`, and clamp to its
maximum. A device without that feature rejects anisotropic samplers instead of
silently using one (the spec would also permit clamping to 1). The C compiler
admits explicit-LOD sampling and implicit fragment sampling under uniform control;
gradient and bias overloads remain unimplemented. The existing anisotropy client
uses explicit LOD and proves admission and a working sampler, without establishing
derivative-driven anisotropic filtering quality.

`tests/mrt.c` renders a sparse four-slot layout at exactly 32 bytes per sample
(RGBA8Unorm additive blend, unused slot, RGBA32Uint, RGBA8Unorm with a green/alpha
write mask) directly and through a sparse-format render bundle, resolves two 4x
slots with discarded sources, and checks exact half-split pixels in every target.
Negatives cover slot count, the byte budget, a subresource in two slots, only-unused
slots, missing/mismatched outputs, blend on an unused slot and a packed pipeline
against a sparse pass. `tests/anisotropy.c` checks zero/non-linear rejection, an
above-maximum clamp and a 16x anisotropic explicit-LOD draw whose 1:1 texel-center
oracle is exact for every filter; it is not a filtering-quality claim.

`tests/implicit_sampling.c` consumes the unchanged compiler fragment fixture over
real four-mip color uploads and rendered depth clears. Its ten cases check 160
literal RGBA8 pixels for derivative-selected LOD, Less/Greater/Equal and a runtime
sampler LOD clamp, plus row/prefix/suffix guards and early releases. Focused CPU
Vulkan execution passes; target SDK validation and physical execution keep their
own source-bound evidence. D3D12 depth textures remain outside its current
constructor envelope. The common bounded fixture reader preserves source bytes
for this client and the select, atomics and explicit-depth controls.

## Intentionally unprovided or rejected

Vulkan and Metal indirect dispatch and nonindexed/indexed indirect pass/bundle
draws use real GPU guards. Metal indexed fetches additionally scan the requested
GPU index windows before one whole native render pass.
Graphics admits up to eight sparse color slots and/or depth/stencil, sample counts 1 and 4, direct
triangle/line lists or strips and every pinned WebGPU vertex format (8/16/32-bit
integer and normalized, float16/32, unorm10-10-10-2, unorm8x4-bgra) with offsets
aligned to min(4, format size).
PointList without device maintenance5, dual-source blending,
and vertex-stage storage writes (fragment writes need enabled fragmentStoresAndAtomics)
remain explicitly rejected. Fragment frag_depth, sample_index and sample_mask are
admitted where the backend reports fragment builtins (`tests/depth.c`). WGSL discard requires actual enabled demote support;
unsupported devices reject that pipeline. Blend constants must be finite f32.
Texture format reinterpretation beyond srgb/unorm viewFormats pairs (RGBA8,
BGRA8; resource-validated, then a mutable Vulkan image with a format list), srgb
viewFormats together with StorageBinding, depth storage, gradient/bias sampling, multisampled
buffer copies/storage, and cube arrays without device imageCubeArray are rejected.
`tests/core_formats.c` round-trips the thirteen formats added for the WebGPU core
set (r8/rg8 snorm/uint/sint, r16/rg16 uint/sint, rgb10a2unorm/uint, rgb9e5ufloat)
through writeTexture and copies, clears the renderable ones and decodes them in
textureLoad exactly.
The baseline admits all 37 WebGPU core color formats (rg11b10ufloat sampled;
renderable only with rg11b10ufloat-renderable) as named Vulkan
color formats, subject to actual device usage/format capabilities. Device
descriptors may request the optional features `src/features.c` reports for the
actual adapter (see Optional device features). Other required features reject
rather than silently succeeding; requiredLimits resolve as described under Limits.
Buffer, sampled/storage image and sampler layouts are
admitted; extensions, binding arrays, specialization constants and
nonbaseline shader capabilities reject. A pipeline layout's immediateSize must
be a multiple of 4 within maxImmediateSize, as WebGPU requires; no shader
declares immediates yet, so the layout needs no push-constant range and nothing
reads it. setImmediates validates its range (4-byte aligned offset and size
within maxImmediateSize) and invalidates the encoder otherwise; a valid call
records nothing a draw or dispatch reads.
ABI16 supplies exact texture/sampler pair-use metadata through helper parameters;
selected layouts validate the actual pairs rather than globally rejecting
unrelated UnfilterableFloat textures and Filtering samplers.
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
real compiler diagnostics for GetCompilationInfo. Successful modules also retain
warning/info notices from [global diagnostic controls](../compiler/DIAGNOSTICS.md).
The native message projection owns its ABI array and borrows immutable module
text; the existing shader lease pins both through deferred callbacks and early
public release. Projection allocation failure reports OutOfMemory before resource
publication. Scoped diagnostic attributes and warnings accumulated before a fatal
validation error are not promised by this transport. `compilation-diagnostics`
exercises severity, deferred lifetime and allocation failure without GPU dispatch.
Compute and render pipeline construction
is synchronous; the async API delivers its actual result through the requested
callback mode, not a claimed background compiler. No blanket success stubs or
fallback provider resolution exist. The public advertised limits
cover the implemented compute/image/graphics path only and do not claim the WebGPU
Core feature level is complete; adapter/device creation remains experimental.
Surfaces on Windows/Android, broader public D3D12 paths
and complete package adoption remain unfinished. Private backend checks do not
qualify those public integrations; see the current Metal scope and bound Apple
evidence above.

The D3D12 texture compute bridge now converts selected compiler HLSL resource and
handle metadata into whole 2D sampled float/uint classes, exact-format storage
SRV/UAV rows and runtime sampler layouts. The five existing color formats remain
the texture constructor envelope; depth, arrays and extra mips/MSAA remain
uncomposed. The new D3D12 procedural graphics bridge handles one RGBA8/BGRA8 UNORM
color target at sample 1, independent VS/PS modules, canonical shared bindings,
runtime samplers, frontFace, viewport/scissor and whole-pass empty/zero-draw
CLEAR/LOAD/STORE/Discard. Stage-visible root tables pack both selected maps into
one resource heap and one sampler heap per descriptor use; module-local ordinals
never identify another module's binding. Discard records an actual zero clear on
the existing texture-use owner, retaining unknown work normally. The new source
has focused portable owner/compiler controls; SDK and driver evidence must be
bound separately before any execution claim. `tests/d3d12_graphics.c` is its
seven-frame yellow/sampler/frontFace/discard/padding/early-release pixel control.
The next direct vertex/instance source increment converts all 41 canonical
formats to real SDK input identities, requires actual IA format support and
preserves holes, required empty slots, zero strides, nonzero ranges and shared
read aliases. Its new RGBA8 `tests/d3d12_vertex_inputs.c` control compares literal
fetched lanes/default components against wrong-expected negatives, checks 86
frames plus padding/canaries and releases dependencies before submission. This
source has host/type/compiler controls only until separate SDK/device evidence
is bound. The retained `tests/vertex_formats.c` endpoint remains unchanged and
also requires RGBA32 float/uint/sint targets. Eight MRT slots, broader color
formats, depth/stencil, MSAA, blend/cull, public fragment UAV writes and GPU-guarded
indexed/indirect graphics remain concrete migration work toward retained parity. Read-only storage binds as SRV while retaining logical StorageBinding
admission; compatible sampled/read-storage aliases use one private allocation
lease and transition. Sampler values are copied into command-owned sampler heaps.
`tests/texture_bindings.c` is the focused public execution control for nearest and
linear sampler replacement, direct/guarded-indirect work, GPU num_workgroups,
read aliases, writable-alias rejection, pixels, padding and early caller release.
`check-d3d12-bindings` checks actual public compiler metadata plus malformed copied
rows without a device. Windows SDK/WARP execution of this newer path requires a
matching source-bound run; prior transfer/buffer receipts do not qualify it.

## Reproduce from a coherent checkout

The actual Windows SDK build route is
`python runtime/c/native/tools/build_windows.py --out C:\doe-build\native --dxc-root C:\doe-tools\dxc`.
It requires a fresh directory outside the checkout and real x64 `clang-cl`/SDK.
Optional `--compiler-dir` reuses the existing compiler builder's audited output;
otherwise that builder runs first. `sources.mk` owns common and Windows-only
provider directories; the Windows builder uses their actual C files and the same
public/extension export inventories. It compiles the official-header DXC C++ seam,
copies the selected x64 DXC runtime DLLs and licenses beside the native DLL, and
records source/tool hashes. It audits the DLL exports and executes only a Win32
typed-loader host control. Its [recorded SDK/public-consumer checkpoint](DEVELOPER_RECEIPT_D3D12_PUBLIC.json)
is source-bound; later changes need their own workflow result and executed scope.
Linux and Darwin builds keep their existing owner set.

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
PASS public view formats: rgba8unorm rendered through its srgb view stores exact sRGB codes 64/128/200/255 (copy and unorm-view textureLoad), rgba8unorm-srgb rendered through its unorm view stores raw bytes, bgra8unorm srgb view keeps byte order, non-pair/unlisted/pipeline-mismatch controls and the srgb-storage limitation, early releases; exact 96 bytes and 4 decoded codes (CPU Vulkan only)
PASS public cube arrays: enabled imageCubeArray, two uniformly filled cubes sampled along all six face axes per array index via texture_cube_array/textureSampleLevel, textureNumLayers, automatic CubeArray layout, partial/cube-dimension view and storage-layout controls, early releases; exact 12 face values and 1 layer count (CPU Vulkan only)
PASS public depth/stencil sampling: rendered depth32float and depth24plus-stencil8 DepthOnly maps via texture_depth_2d (automatic depth layout) and texture_2d<f32> (explicit unfilterable-float), StencilOnly as texture_2d<u32>, two-layer texture_depth_2d_array, texture_depth_cube dimensions, comparison-sampler binding admission, 7 view/binding/creation controls, early releases; exact 48 depth values, 8 stencil values and 2 dimensions (comparison sampling has its own depth-comparison control) (CPU Vulkan only)
PASS public depth/stencil copies: rendered depth32float .25/.75 via DepthOnly/All readback and texture copy, depth16unorm rendered 0/65535, writeTexture and buffer upload plus texture copy, stencil8 upload then IncrementClamp, depth24plus-stencil8 stencil Replace and StencilOnly upload, depth24plus texture copy observed by depth test, 11 copy and 1 creation validation controls, early releases; exact 72 depth/stencil texels and 8 color pixels (CPU Vulkan only)
PASS public indexed draws: uint16/uint32 indices, firstIndex, signed base vertex, instance selection, offset index ranges, pass/bundle replay, uint16/uint32 strip primitive restart, 9 GPU-produced drawIndexedIndirect records with exact-end/out-of-range/wrapping/zero/firstInstance/instance-overrun guards, unchanged arguments, 13 typed validation controls and early releases; exact 192 pixels (CPU Vulkan only)
PASS public ABI9 atomics: storage accumulation, signed OOB suppression, compare-exchange result identity, fresh workgroup initialization across two groups and two dispatches, early releases (CPU Vulkan only)
PASS public debug markers: typed procedures, independent nested balance, parent lock, underflow, malformed strings and finished/unbalanced command/pass/bundle rejection (CPU Vulkan only)
PASS public multisampled reads: 4x rgba8unorm stored with sample masks 0x5/0xE and alpha-to-coverage 0/1, every sample of 8 texels read via texture_multisampled_2d textureLoad under the automatic unfilterable-float multisampled layout, textureNumSamples, out-of-range sample zero, a whole 4x texture copy read back per sample, a multisampled read beside a filtering-sampler linear sample in one pipeline, float-layout/storage/single-sample-binding and 4x buffer/count-mismatch/partial copy controls, early releases; exact 96 samples (CPU Vulkan only)
PASS public attachment discard: single-sample color discard then direct copy, Load pass and compute textureLoad all read GPU zeros, Store control keeps green, discarded depth32float copies as 0.0, early releases; exact 40 texels and 32 loaded components (CPU Vulkan only)
PASS public point lists: enabled maintenance5 default point size, 9 one-pixel points at exact centers from a direct draw, a render bundle and an indexed draw with an unselected decoy, stripIndexFormat control, early releases; exact 32 pixels (CPU Vulkan only)
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
PASS public format features: 4 of 4 offered and enabled (float32-filterable linear r32float/rgba32float midpoints exact, float32-blendable additive r32float 1000007.0, depth32float-stencil8 depth 0.25/stencil 7 per aspect, rg11b10ufloat-renderable blended (1.25,1,2.25) and 4x-resolved (1,.5,.25) packed bits); without them depth32float-stencil8 texture/pipeline/bundle, r32float blending, 'float' r32float binding and rg11b10ufloat attachment/target/bundle reject while rg11b10ufloat still samples exactly, an unoffered feature fails the request (CPU Vulkan only)
PASS public draw features: 2 of 2 offered and enabled (indirect-first-instance: GPU firstInstance 2/1 reaches instance attributes and instance_index exactly for indirect, indexed indirect and bundle draws, an instance-buffer overrun stays a no-op; depth-clip-control: unclippedDepth draws z 1.5/-0.5 clamped to depth 1.0/0.0 where the clipped control draws nothing); without them nonzero indirect firstInstance is a no-op while direct firstInstance draws, and unclippedDepth rejects (CPU Vulkan only)
PASS public copy layouts: undefined rowsPerImage for one image and undefined bytesPerRow for one row through queue writes, buffer-to-texture and texture-to-buffer copies with exact texels; two-image and two-row undefined-stride and rowsPerImage-0 rejections reported once at Finish (CPU Vulkan only)
PASS public BC compression: solid BC1/BC1-srgb/BC4 blocks decode to exact texels through queue writes and buffer copies, a 6x6 mip takes its physical 8x8 copy, texture-to-texture and texture-to-buffer copies return exact blocks, copies whose block reaches only one side's edge (both directions) land exactly, writeTexture of whole blocks into a 2x2 mip and with odd offsets and bytesPerRow 8*w+3 (two layers, rowsPerImage 3), bc1/bc7 srgb view formats, a zeroed 8192x8192x8 BC1 texture, a 3D BC1 texture uploaded with rowsPerImage 2 decodes per slice (sliced 3D); without the feature, a partial-block writeTexture, non-block sizes, attachment/storage usage, 3D without sliced 3D and unaligned copies reject (CPU Vulkan only)
PASS public component swizzle: (b,a,0,r) and (1,g,r,g) sampled views return the identity view's rgba8unorm components rearranged exactly through textureLoad and nearest textureSampleLevel; identity swizzles need no feature, a non-identity swizzle without the feature, with RenderAttachment or StorageBinding view usage, or with an unknown source rejects (CPU Vulkan only)
PASS public encoder errors: an invalid render/compute pass descriptor or command, an invalid copy or clear followed by valid passes, each reports exactly one uncaptured validation error at Finish; End/dispatch on ended passes, copies/clears/begin on a finished encoder and a second Finish report at the call; render bundle encoders: invalid setPipeline/draw give one error at bundle Finish and an invalid bundle, executing it in a valid pass gives one error at the command encoder's Finish, commands and a second Finish after bundle Finish report at the call, ended passes on a valid encoder report at the call and leave it valid, End with an unlocked parent and invalid-command-buffer submits report, a chained Finish ends the encoder, an invalid bundle encoder descriptor reports at creation and at its Finish and its bundle once at the executing encoder's Finish, a failed submit invalidates its whole batch in either order; core-features-and-limits listed and accepted (CPU Vulkan only)
PASS public core formats: 13 added core color formats round-trip exact bytes through writeTexture and texture/buffer copies, 10 renderable ones clear to exact encodings, snorm/rgb9e5/rgb10a2/int textureLoad decodes exactly, and non-renderable attachments (r8snorm, rgba8snorm, rgb9e5ufloat), multisampled r32uint/rg32float and an r8uint resolve reject (CPU Vulkan only)
PASS public timestamps: requested feature, zero/max/range/destroy admission, real pass/command writes, exact GPU nanoseconds, same-batch copy, scratch reuse and early query release (CPU Vulkan only)
PASS typed recovery: native/OOM/pending errors survive submit/write/texture/workdone, unknown leases retained, terminal cleanup and fresh GPU normalizer recovery
PASS every implemented procedure address matches; every unprovided name returns NULL
PASS exact exports: 192 implemented, 10 pinned unprovided, no private exports
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
leak detection does not claim leak qualification. On 2026-10-23 at 301fef689 on Jupiter (no ptrace),
the same instrumented `check` on lavapipe also passed all 29 checks with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`, with LeakSanitizer confirmed active
by a deliberate-leak control. That is leak evidence for these software-adapter
clients only, not for physical drivers or untested paths. `DEVELOPER_RECEIPT_IMAGES.json`
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

## Doe shader-check extension

`include/doe_native_shader_check.h` declares the device-less WGSL check the addon's
`checkShaderSource` (doe-gpu `preflightShaderSource`) resolves, listed in
`EXTENSION_SYMBOLS.txt` with the same rules as the program extension. The check runs
the whole-module validation and emission `wgpuDeviceCreateShaderModule` runs for the
platform's default target (Metal on Apple, SPIR-V elsewhere) and discards the module.
A rejection keeps its compiler diagnostic, stage (`parser`, `sema`, `unsupported`,
`emit`, `native_check`), kind and 1-based position for the calling thread until its
next check or binding reflection; the copy calls report no other operation's
diagnostics. `tests/shader_check.c` exercises acceptance, parse and semantic
rejection, truncated copies and a null source; `test-integration-shader-preflight.js`
checks the same contract through doe-gpu on every provider.

## Doe queue sync-info extension

`include/doe_native_queue.h` declares `doeNativeQueueSyncInfo`, the retained Zig
export the addon's `queueSyncInfo` (doe-gpu `nativeQueueSyncInfo`) resolves, with
the same bit values (listed in `EXTENSION_SYMBOLS.txt`). A Vulkan queue reports
`BACKEND_VULKAN`, never `TIMELINE_SEMAPHORE` or `FENCE_POOL` (each submission owns
one fence), and `DEFERRED_SUBMISSIONS` while a submitted command record remains on
the pending list, that is, until a public wait or the progress worker observes its
completion or failure. Other backends and a NULL queue report 0. Because the
worker settles in the background, a caller sees the deferred bit only for work not
yet observed, never a guaranteed state after submit. `tests/queue_sync_info.c`
withholds fence observation at the real poll boundary to check the bit while work
is unobserved and its clearing by work done and by host access to a used buffer.

## Doe adapter PCI identity extension

`include/doe_native_adapter.h` declares `doeNativeAdapterGetPciIdentity`, the
retained Zig export the addon resolves for `GPUAdapter.info` `vendorID`,
`deviceID` and `driverVersion` (listed in `EXTENSION_SYMBOLS.txt`). Vulkan reports
`VkPhysicalDeviceProperties` vendorID, deviceID and the raw driverVersion; D3D12
reports its vendor and device IDs and driver version 0 (not recorded); Metal and a
NULL adapter report 0 for every field. Without the extension the addon still
reports the standard `WGPUAdapterInfo` IDs and omits `driverVersion`.
`tests/adapter_info.c` checks the IDs against `wgpuAdapterGetInfo`, a nonzero
Vulkan driver version and zeros for NULL; doe-gpu's
`test-integration-adapter-pci-identity.js` compares all three with `vulkaninfo`.

## Doe fused compute command extension

`include/doe_native_commands.h` declares
`doeNativeCreateComputeDispatchCopyCommandBuffer` and
`doeNativeCreateComputeDispatchBatchCopyCommandBuffer` with the retained Zig
signatures that `runtime/zig/tests/native_recorded_compute.c` calls (listed in
`EXTENSION_SYMBOLS.txt`; the addon resolves neither). Each records, under one device
lock, a compute pass with one or more dispatches (bind group slots 0..count-1, at
most 4 per dispatch, NULL slots unbound) and then `copyBufferToBuffer`, through the
same resource encoder, pass, dispatch-limit and copy validation as the public calls,
and returns only a successful Finish. The command buffer holds its own leases, so
the caller may release every input before submission. A rejection returns NULL and
reports one `compute command construction failed: <status>` error (validation, or
out of memory); a NULL device or an empty batch returns NULL and reports nothing.
`tests/fused_commands.c` checks dispatch-before-copy results, a two-dispatch batch,
caller release before submit and each rejection.

## Doe binding-reflection extension

`include/doe_native_reflection.h` declares `doeNativeShaderModuleGetBindings` and
`doeNativeShaderModuleGetBindingsForEntryPoint`, the retained Zig exports the addon's
`shaderModuleGetBindings*` resolve, with the same five-field record and value codes
(listed in `EXTENSION_SYMBOLS.txt`). An entry's bindings are the compiler's resource
closure from the same `doe_shader_specialize` selection pipeline creation uses; the
whole module is the union over its declared entries, each (group, binding) once. No
second WGSL parser or binding table exists. Failure returns `SIZE_MAX` with the
reflection diagnostic copied through the shader-check calls: an invalid module, an
unknown entry name, or an entry the compiler cannot specialize without pipeline
constants. Unlike the retained provider, resources no entry uses are not reported,
since the compiler reflects per-entry closures only. `tests/shader_reflection.c`
covers buffers, uniforms, textures, samplers and a write-only storage texture.

Adapter info reports the vendor from the PCI/Khronos vendor ID (lowercase, empty
when unknown, `mesa` for lavapipe), an empty architecture (no family source), and
the driver's device name as both `device` and `description`; the strings share one
allocation that `wgpuAdapterInfoFreeMembers` releases (`tests/adapter_info.c`).
No C backend offers the `subgroups` feature, so `subgroupMinSize` and
`subgroupMaxSize` are the WebGPU defaults 4 and 128, not the driver's subgroup
size; real sizes belong with that feature once a backend offers it.

## Doe compute-program extension

`include/doe_native_programs.h` declares the doe-gpu native program contract the
retained Zig provider exports, listed separately in `EXTENSION_SYMBOLS.txt` (never
webgpu.h names, never resolved by `wgpuGetProcAddress`; `tests/audit-exports.py`
checks both inventories). The contract version is
`config/compute-program.schema.json` `$defs.nativeContractVersion`, read at build time.
Reusable GPU recordings require Vulkan. Host replay uses the selected backend's
existing native lowering; Metal compute/buffer packets are composed too.

- `doeNativeComputeProgramPrepare` (gpu-recorded) claims a finished, never-submitted
  command buffer in the resource owner and records ONE reusable Vulkan command buffer
  (buffer copies, clears, direct dispatches, timestamp writes including compute-pass
  timestampWrites, and query resolves), sealed so preparation
  dispatches nothing and buffers stay writable before the first run.
- `doeNativeComputeProgramPrepareHostReplay` (native-recorded) claims the same way;
  every submit lowers the retained commands into a fresh one-shot native recording.
  Its advertised entrypoint is backend-generic: Vulkan and Metal use their tagged
  native command owners, without reading another backend's union payload.
- `doeNativeComputeProgramSubmit` revalidates live, unmapped, healthy dependencies
  and submits once through the existing pending owner; mapping between runs is
  allowed. Unknown completion keeps the replay and the recording alive.
- Standard `wgpuQueueSubmit` keeps WebGPU single-use semantics and rejects claimed
  command buffers. Timestamp queries inside a GPU recording must be self-contained:
  each write lowers to reset+write, and a resolve of an index the recording never
  wrote is rejected at preparation (host replay admits it). Each run re-acquires the
  query pool and checks its health like buffer uses; completion publishes the
  recorded indices, so later ordinary resolves see the last run's values, and the
  GPU-nanosecond normalizer's scratch and pipeline are held by the recording.
  `doeNativeComputeProgramTimestampNanoseconds` reports period 1 / 64 bits because
  queries already resolve to GPU nanoseconds.

`tests/compute_programs.c` checks three exact runs per mode with changing inputs and
mapped readback between runs, single-use rejection, claim-twice, destroyed
dependency, early public releases and GPU-recording admission. Its gpu-recorded
timestamp program checks three runs of strictly increasing, nonzero GPU-ns intervals,
that an ordinary resolve afterwards returns the last run's exact values and new
ordinary writes advance, that the program still runs after that ordinary use, and
that a resolve-only recording is rejected.
`tests/metal_bridge.c` checks genuine Metal public preparation/replay through the
native call ledger, failed constructor/encoding cleanup, and program release while
completion is unknown. It does not simulate shader results.

## Physical developer runs and holds

Physical runs require owner authorization and current cooling readiness. Miles is
authorized for bounded D3D12 developer runs; its
[private backend guide](../backends/d3d12/README.md) owns that route.
Every adapter-using client selects its adapter through
`tests/adapter_class.h`: software (CPU fallback adapter) by default, or a
nonfallback GPU adapter with `DOE_C_TEST_ADAPTER=physical`. Each PASS line names
the class and device it ran on. `make check-physical` runs the declared clients on
the system ICDs (`VK_ICD_FILENAMES` unset):

```
make -C runtime/c/native BUILD=/tmp/doe-c-native check-physical
```

Adapter requests select the first Vulkan device of the requested class (the CPU
device only for `forceFallbackAdapter`), never ordinal 0 filtered afterwards, and
device creation reopens exactly the adapter's hardware by
the `VkPhysicalDeviceIDProperties` deviceUUID/driverUUID pair. Reordered
enumeration preserves that same identity; missing, ambiguous or changed-driver
identity fails without substitution. The earlier deviceUUID-only source at
`a0dbcb970` was exercised on Jupiter with both RADV and lavapipe ICDs in either
order: requested classes were selected correctly. The stronger UUID-pair controls
and their execution scope are recorded in the identity/error receipt.

On 2026-10-23 all 18 passed on Jupiter: AMD Radeon 8060S Graphics (RADV
STRIX_HALO, vendor 0x1002 device 0x1586), Mesa 26.0.3-1ubuntu1 RADV, Vulkan 1.4.335,
Linux 7.0.0-38. After indexed drawing was added, all 19 clients (including
`indexed-render`) passed again on that same adapter/driver the same day, and after
depth/stencil copies were added (with the WGSL language-feature and texture-binding
view clients) all 22 (including `depth-copies`) passed; with depth sampling all 23
(including `depth-sampling`) passed; with cube arrays all 24 (including
`cube-array`) passed; with srgb view formats, multiple color attachments and anisotropy all 27
(including `view-formats`, `mrt` and `anisotropy`) passed; with compute programs,
vertex formats, storage textures, point lists, attachment discard and multisampled
reads all 33 (including `storage-textures`, `point-list`, `color-discard` and
`msaa-sampling`) passed; with optional format features all 34 (including
`format-features`) passed; with optional draw features all 35 (including
`draw-features`) passed; with undefined copy strides all 36 (including
`copy-layouts`) passed; with limits resolution all 38 (including
`program-prepare` and `limits`) passed; with BC compression all 39 (including
`bc-compression`) passed; with component swizzle all 40 (including
`component-swizzle`) passed; with the encoding error rule all 41 (including
`encoder-errors`) passed; after the BC review fixes all 42 passed; with the core formats all 43
(including `core-formats`) passed. That is
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
software Vulkan results and provide no Metal execution coverage. The later
[Metal draw-guard receipt](DEVELOPER_RECEIPT_METAL_DRAW_GUARDS.json) retains its
own published source and actual M3 consumer results.

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

Native command and pipeline failures carry an owned status/operation/native-code/
diagnostic snapshot through the existing pending owner. Background terminal errors
remain available to the next public observation after command release. Public
messages are formatted after unlocking and copied into deferred pipeline, queue
and map callback payloads; native diagnostic strings never escape their lifetime.
`tests/backend_failure.c` exercises wait/poll/recover transport over the retained
Metal call boundary without GPU execution. `tests/metal_bridge.c` checks exact
public synchronous and deferred messages; `tests/pending_progress.c` checks a
background Vulkan detail is observed once. These are developer correctness
controls, separate from physical failure/recovery qualification.

Stage-4 robustness controls on software Vulkan:
- `tests/fault_sweep.c` runs one workload per fresh device. It has four rounds:
  - compute: an init-batch texture, buffers, writes, a shader, a pipeline, a bind
    group, a direct and an indirect dispatch, and copies;
  - render: a render pipeline, a pass with an occlusion query around a direct
    draw plus an indirect draw, a query resolve and a readback;
  - compute program: prepared and submitted twice;
  - compute again, reusing the retained pipeline and the spare pool.

  Each round submits with immediate caller release, then requests work done and
  a map. Each case fails one thing in turn:
  - every host allocation of the C owners (GNU `--wrap` of malloc/calloc);
  - every result-returning Vulkan call, through the device context's function
    table. Creation and allocation calls report out-of-memory, `vkQueueSubmit`
    refuses, and fence, wait and idle calls report device loss while the real
    queue may still be running;
  - every host allocation from `wgpuCreateInstance` through requestAdapter,
    requestDevice and one compute round. A failed creation must arrive through
    its request callback.

  Every case must settle without a crash or hang. Every map completes, a
  successful map is exact, every admitted work-done future is delivered, and a
  result-changing or refused-callback failure is reported. Device release
  reclaims everything; the leak half runs under ASan.
- `tests/concurrency.c` runs work from several threads. Four threads on one
  device and two on their own devices create, record, submit, map and release
  (half immediately after submit). Their rounds cover direct and indirect
  compute with fresh and shared pipelines, init-batch textures, render rounds
  (fresh render pipelines, occlusion queries, direct and indirect draws) and
  compute programs. Meanwhile a poller thread requests work done, and another
  thread requests, uses and releases 24 adapter/device pairs. Then three threads
  keep copying and mapping while another thread destroys the device: maps
  settle, one loss callback runs.
- `tests/lost_objects.c`: invalid objects after destroy() (see above).
- `make check-sanitizers` builds sanitized libraries. It runs the concurrency
  test under TSan and under ASan+UBSan, and the fault sweep and lost objects
  under ASan+UBSan with leak detection. It is separate from `check`.
  `tests/tsan.supp` suppresses only reports whose stacks lie entirely in
  uninstrumented third-party code: lavapipe's own threads, libgcc's unwinder
  registry under LLVM's JIT, and the loader/LLVM lock order.

The Metal frontend now composes real initialized textures/views/samplers, color
copies and packed QueueWriteTexture uploads, compiler-reflected vertex/fragment
pipelines and direct, indexed and GPU-guarded indirect whole passes. Vertex and
fragment use their own
compiler-provided binding tables; vertex-fetch slots avoid shader buffer/size
slots. Shared public fixed/depth/interface admission is converted explicitly to
Metal without synthetic Vulkan capabilities. Depth/stencil transfers consume
resource-normalized aspect layouts, retaining forbidden depth uploads and opaque
Plus-depth readback validation. Depth bias, unclipped depth and fragment
depth/sample builtins retain shared validation and native per-draw state. Fixed
sample masks use compiler-owned fragment output lowering. A partial mask with
no fragment stage still rejects: the explicit absent-fragment contract is not
replaced by a synthetic fragment shader.
Texture initialization, discard recovery and public submissions share the same
backend aspect state and tagged pending owner. An initialization that becomes
unknown does not publish a public texture and cannot drop its private allocation.

`offscreen-triangle` is one public consumer for Vulkan (the default) and Metal:
`DOE_C_TEST_BACKEND=metal DOE_C_TEST_ADAPTER=physical` selects Metal explicitly.
It checks ordinary WGSL vertex_index rendering into RGBA8, asymmetric triangle
pixels, public early releases, discard followed by zero readback and packed upload
roundtrip. The Metal adapter prints its actual name and truthful unclassified
subtype. These focused developer results do not qualify Metal graphics, broader
platforms, performance or a production provider switch. The host-only
`metal-bridge` control separately checks native initialization failure detail,
unknown-work private lifetime and direct-render call identity without GPU emulation.

`mixed-resources` uses the same public consumer on default CPU Vulkan and explicit
Metal. It exercises four groups and binding 999 with uniform/storage buffers,
sampled/storage textures and a sampler, independent buffer/image output checks
and early public releases. The native shader policy explicitly selects the
compiler's core binding-number namespace; standalone compiler defaults retain
legacy numeric admission. Numeric binding labels do not consume native slots.
ABI12 provides separate per-entry resource rows and runtime-tail size indices;
compute and graphics consume one selected-entry map and typed snapshot owner.
`metal-bridge` additionally rejects forged entry/table/slot/tail-size metadata and
checks native failure delivery, unknown completion and final resource retirement.

`doe_native_device_policy.h` provides a versioned per-request policy extension.
It shares ordinary request-device callbacks and loss/lifetime handling, reopens
only the selected Vulkan adapter's device/driver UUID pair or the same retained
Metal device identity or exact D3D12 adapter LUID, and never mutates the adapter
or process defaults. Vulkan queue selection, fence/timeline synchronization and
queue-write upload mechanisms are explicit. Staged queue writes record a real
copy from a private host buffer and submit through the existing pending owner;
unknown completion retains those leases. Subgroup-size decisions remain with
selected-entry reflection and the backend's exact pipeline cache key. Unsupported
values/backend tuples fail device creation. `doeNativeQueueSyncInfo` reports the
actual synchronization mode; the default fence pool was already present before
its previously missing reporting bit was corrected. Metal policies use its real
unified graphics/compute queue, optional shared-event timeline and either an
actual staged blit upload or existing mapped writes. Event-unavailable preference
fallback is reported as ordinary command completion, never as a fictitious
timeline. Metal compute-only/fence-pool/subgroup requests are rejected.
`check-metal-device-policy` exercises this public route with typed host calls,
not GPU results; backend `check-timeline` covers event/error/lifetime ordering.
D3D12 policy admission reuses the existing direct queue, monotonic native fence,
and UPLOAD-to-DEFAULT copy/pending owner. No new queue or upload engine exists.
Compute-only/fence-pool/direct-mapped/subgroup requests fail before reopening the
same selected LUID. `check-d3d12-device-policy` checks exact tuple admission and
unchanged outputs on original reopen failure; it is a host boundary control,
not Windows or WARP execution. Queue-sync backend bits distinguish all three
backends, while timeline and pending bits describe their actual owners.

The same optional pipeline-cache extension now routes Vulkan to its real driver
`VkPipelineCache` owner as well as Metal to `MTLBinaryArchive`. Vulkan supports
explicit bounded persistence/reload with exact driver/device identity and a
corruption checksum; D3D12 persistence remains unsupported. Counters describe
native operations only. Cache failures do not issue WebGPU error callbacks or
replace a command's first error, and disabling never writes a cache file.


### Canonical half stage interfaces and feature composition

Stage-interface reflection retains the WGSL f16 identity while each target uses
an f32 input/output carrier and explicit boundary conversion. Native vertex fetch
and fragment target checks accept both floating scalar widths; interstage matching
still requires equal canonical widths, lanes and interpolation. The public
`half-interfaces` control creates real CPU Vulkan pipelines for f32/f16 vertex
fetch, half fragment output and alpha coverage, and rejects integer-format and
f16/f32 interface mismatches. This is pipeline composition, not pixel-output or
physical-device qualification.

The public WGSL feature probe admits pointer-composite access, immutable
texture/sampler lets and immediate address space with and without `requires`.
Unrestricted pointer parameters now use canonical root/argument specialization
and pointer-content uniformity; the unchanged public language-feature consumer
creates real CPU Vulkan pipelines both with and without `requires`. Six of the
12 pinned language features are reported. Subgroup language exposure remains a
separate composition gate, not implied by these pointer controls. D3D12 ShaderF16 advertisement requires
actual native-16-bit operations and shader model 6.2, not feature-level inference.
Queried BC and depth32float-stencil8 support remain independent flags. The
`capability-admission` host control covers these combinations and distinct
Vulkan variable-pointer-storage/full capability gates without emulating a device.

### D3D12 HWND surfaces

The Windows source route accepts `WGPUSurfaceSourceWindowsHWND`, selects the
requested D3D12 adapter without fallback, queries actual DXGI/format support,
and composes acquisition, zero initialization, rendering/copies and presentation
through the existing native pending and resource texture owners. A frame's
ordinary texture/view leases retain its actual backbuffer and swapchain.
Failed publication returns an executed acquisition through the same command owner;
unknown completion is retained, never treated as safe cleanup.

The initial surface contract is opaque HWND, BGRA8/RGBA8 linear or sRGB, FIFO
and queried tearing-enabled Immediate. Alternate view formats and changing
swapchain buffer usage require an explicit supported configuration; cross-device
replacement requires unconfigure. Resize rejects retained frame/view/command
leases and preserves the previous configuration. A failed/unresolved presentation
requires reconfiguration after retirement. Backend host call controls and a Linux
common-source build are not Windows SDK or actual HWND presentation evidence;
those execution gates remain unqualified on dot.

The actual Windows native producer builds `windows_surface.exe` against its
produced DLL and the existing CLI HWND host, and records the fixture/source hashes;
it does not run the fixture. Explicit execution requires a Windows desktop and
`DOE_C_TEST_BACKEND=d3d12`; default `DOE_C_TEST_ADAPTER=software` requests WARP,
while `physical` is a separate opt-in adapter scope. The shared surface consumer
checks real pixel readback, zero initialization, invalid-config preservation,
retained-view resize rejection, successful resize after release and teardown.

### External-texture native ABI and import

`doe_native_external_texture.h` declares the retained creation/import/lifecycle
extensions. Public `wgpuExternalTextureAddRef`, `Release` and `SetLabel` share the
canonical resource owner, and a chained `WGPUExternalTextureBindingEntry` binds
its exact external object. View-backed creation retains one or two same-device
planes. Metal native chains import IOSurface/CoreVideo planes through the normal
Metal texture/view owners. Unsupported source formats/backends and failed native
imports report errors without falling back to descriptor views.

Compiler external identity survives automatic/explicit layouts and canonical
command leases. Vulkan, Metal and D3D12 resolve the compiler-selected sampled
texture slot to plane zero; both planes stay retained for two-plane objects.
This explicitly preserves the retained plane-zero compatibility behavior. It is
**not YUV conversion or complete WebGPU external-color semantics**, and no
video-correctness or native physical-import qualification follows.

The standard `wgpuDeviceCreateExternalTexture` browser descriptor is a separate,
complete 112-byte ABI matching Chromium `4a6fb385` / Dawn `758e5e6a`. It admits
full-view, single-plane, four-component float sampling with identity conversion
or `doYuvToRgbConversionOnly`. Required conversion spans remain required. Crop,
scale, rotation, mirroring and nonidentity color conversion reject through a
real error external-texture resource; label/release are safe, but binding and
refresh fail. The private `DoeExternalTextureDescriptor` retains its 40-byte
prefix contract. Neither interface silently promises conversion from plane-zero
sampling. The CPU ABI audit checks the full browser descriptor and signatures;
browser execution and color semantics require their own output checks.

`make check-external-textures` covers public expiry/refresh/destroy, label and
reference lifecycle, chained binding, cross-device rejection, import failure,
early release, and exact plane-zero sample readback on the selected adapter.
The existing Metal host bridge additionally exercises native import rollback,
resource adoption, compiler slot identity and both-plane pending retention.
`make -C ../backends/metal check-external` owns the native-call ledger controls;
Apple SDK and physical IOSurface/CVPixelBuffer execution remain separate gates.


### Retained graphics extensions and admission composition

Clip-distance arrays, fragment primitive indices and dual-source outputs retain
canonical stage metadata through Metal, HLSL and SPIR-V. Native modules require
that the corresponding optional device feature was requested. Vulkan uses actual
enabled clip-distance, geometry and dual-source feature bits. Metal clip distances
are baseline; primitive index requires Apple7/Mac2 with the matching language
baseline, and dual-source blending requires Apple2/Mac2. D3D12 uses the admitted
FL11+ stage/blend envelope. Source1 blends require the actual second output and
one color target, rather than treating a second render target as source1.

The public `stage-extensions` control has created a real CPU Vulkan pipeline and
rejected missing secondary output and multiple-target misuse. It is not a pixel
oracle or Windows/Apple hardware qualification. Source spans beyond 64 KiB use the
compiler's bounded address representation and unchanged semantic capacities;
`shader-source-span` proves copied ownership and a real pipeline after the caller
releases its large input. The old small-source cutoff is not a retained contract.

D3D12 device feature composition never silently grants CoreFeaturesAndLimits when
the adapter lacks it. Its 64-byte immediate limit now matches the composed root
constant transport. Immediate HLSL uses flat scalar fields, not dynamically indexed
root-constant arrays. Root constants/root CBVs consume root-signature DWORDs, not
the descriptor-table CBV limit; graphics UAV counts cover both actual stage tables.
Fragment writable storage uses the admitted FL11+/SM6 path; vertex writes remain
rejected. GPU-normalized indexed/indirect argument records carry actual base-vertex
and first-instance values to the pipeline-owned root-constant command signature.

### Pretranslated native shader sources

`doe_native_shader_sources.h` describes the retained contextual MSL/HLSL shader
chains; upstream `WGPUShaderSourceSPIRV` carries SPIR-V words. Modules copy source
bytes and pipelines retain the module owner after caller handles are released.
MSL validates its library at module creation through the same Metal compiler
owner used for pipelines. HLSL compilation is deferred to genuine DXC pipeline
creation. SPIR-V framing is checked at creation; compute entry selection requires
an unambiguous fixed `LocalSize` (not `LocalSizeId`). Raw SPIR-V render imports are
not admitted.

These imports have an explicit native/degraded contract: no WGSL reflection,
robustness, sampling-pair completeness, or TranslationInfo proof is invented.
Automatic layouts are empty, not proof that native code uses no resources.
Explicit layouts describe resources and retain ordinary device limits and
binding ownership. MSL uses separate native tables with slot `group * 32 +
binding`; SPIR-V uses its declared descriptor sets/bindings. Raw HLSL currently
admits only empty-resource layouts, without guessing structured-buffer strides
or register mappings. WGSL override constants and generated immediate-data
carriers do not apply. MSL/HLSL zero workgroup dimensions normalize to one.
Opaque graphics interfaces defer native interface compatibility to the actual
backend pipeline constructor; fixed-state validation still applies. Imported
Metal fragments do not receive generated sample-mask injection, so genuinely
partial active masks remain unsupported.

Named controls cover actual CPU Vulkan SPIR-V dispatch/readback after caller
source/module release and Metal constructor/slot/lifetime call ledgers. Metal
mock ledgers are not GPU execution evidence; Windows DXC execution remains a
separate SDK/platform gate.

### Retained optional-format closure

The finite native feature audit covers 22 retained offered features: core limits,
DepthClipControl, Depth32FloatStencil8, BC and sliced BC, ETC2, ASTC and sliced
ASTC, timestamps, indirect first instance, shader f16, RG11 rendering, BGRA
storage, float32 filtering/blending, clip distances, dual-source blending,
subgroups, texture format tiers 1/2, primitive index, and component swizzles.
All have source-composed offered/requested/admitted paths where the backend
supports them; this inventory is not hardware qualification. ETC2/ASTC remain
unsupported on D3D12, rather than advertised through another implementation.

BGRA storage and Tier1 use the canonical resource format predicates for texture,
layout, and render admission. Six normalized-16 formats map through the normal
Vulkan/Metal/D3D12 format owners. Tier1's 23 additional storage formats and nine
additional render formats are enabled only for a requested feature. Their
backend support is independent of compiler vocabulary: retained and C WGSL both
accept the same 16 core storage format spellings; this change adds no WGSL
format syntax. Native pretranslated inputs retain their documented contracts.

Tier2 now has meaningful, requested-feature admission for its specified 15
read-write formats. Baseline r32 read-write remains available. This deliberately
corrects retained blanket core-format read-write admission: translation success
is not evidence that arbitrary formats support native read/write. Tier2 implies
Tier1, which implies RG11 rendering; sliced compression implies its base
compression feature. Required features use set semantics: repeating a supported
name preserves the same enabled capabilities. An unknown or unsupported member
still rejects the whole request without publishing a partially enabled set.
`adapter-info` checks both synchronous and callback-based constructors, including
64 repetitions followed by an unknown-member negative control.

Vulkan intersects each optional requirement with actual format queries and the
enabled extended-storage feature. A driver lacking signed-normalized blending
cannot offer Tier1 or its dependent Tier2 even if storage operations alone work.
The CPU llvmpipe validation environment exercises BGRA creation and disabled
feature rejection; its queried signed-normalized formats lack blending, so the
Tier2 execution control reports unsupported rather than overriding admission.
Metal uses supported family format bounds, actual multisample/resolve support,
and `readWriteTextureSupport` for Tier2. D3D12 queries the actual format support
bits and sample quality; write-only UAV bindings do not spuriously require typed
UAV loads, while read-write bindings do. Optional-disabled BC/depth/normalized
format creation now passes the same shared gate as other backends. Sampled
D3D12 swizzles use real SRV component mappings, not shader substitution.

D3D12 CoreFeaturesAndLimits is offered only when its reported frontend limits
meet every canonical core default and queried baseline formats meet the shared
format-properties requirements. Missing query facts keep it absent. The device
never gains implicit Core when the adapter does not offer it. Windows SDK and
physical-device qualification remain separate from the portable composition
controls and strict host builds.

### Retained legacy ABI boundary

The [shared Metal memory/fence boundary](SHARED_MEMORY.md) documents native-handle
ownership, producer synchronization and retained initialization-policy limits.

`include/doe_native_legacy.h` and `LEGACY_SYMBOLS.txt` describe additive retained
Doe entrypoints, separately from the pinned WebGPU header. The procedure table,
platform exports and export audit use the explicit inventory; no fallback provider
resolves these names. The legacy uncaptured callback takes **no device argument**,
unlike the pinned descriptor callback. Getters return retained handles. DeviceTick
performs nonblocking progress and returns `void`, matching the selected Dawn ABI.

`wgpuCommandEncoderWriteBuffer` snapshots caller bytes into an ordinary owned
staging buffer and records a copy at that exact command position. Recording does
not modify the destination. Command and pending owners preserve staging lifetime
through submission and uncertain native completion. Its pointer/size signature
matches the selected Dawn ABI. Both legacy pass WriteTimestamp entrypoints
preserve the retained locked-parent rejection. Actual pass timestamps use descriptor
`timestampWrites`; exposing the rejecting aliases does not advertise arbitrary
in-pass timestamp support. The unadvertised Dawn format-capability query rejects
without touching output; its default-empty free-member helpers remain local.

Browser texture-copy adapters implement byte copies and flipY through ordinary
commands. External copies use plane zero with the view's mip/layer bounds, retain
real texture storage through submission, and reject expired/wrong-device objects.
They do not perform YUV, alpha or color-space conversion; conversion requests now
fail explicitly rather than copying pixels while implying conversion. This is a
correctness restriction relative to retained Zig's ignored conversion fields.
Imported Metal external planes admit real CopySrc blits. These source contracts
and CPU Vulkan controls do not qualify physical Metal or an actual Fawn browser.

The retained native public-symbol reconciliation is finite: 241 exported `wgpu*`
names plus 25 additional explicit lookup names = 266. The C inventories provide 250
(202 pinned + 23 legacy + 25 shared-memory compatibility names). The remaining 16
are not implemented retained native features: resource-table creation, seven
resource-table methods and three pass/bundle setters (11); two multi-draw calls;
pixel-local-storage barrier; texel-buffer-view AddRef; and the no-op logging setter.
The first 15 resolve only through foreign delegation or fail, with no Doe native
owner; logging does nothing. None is advertised as implemented C support, and no
success stub is added for symbol-count equality. Positive capabilities, rejecting
compatibility contracts and actual physical execution remain distinct evidence.

### Empty texture-copy submission ownership

An empty texture-to-texture copy still records both resource uses. The canonical
resource command owns their leases through finish/submission, so destroying
either texture after encoding produces submission validation; releasing only
public references remains safe. Existing Vulkan, Metal and D3D12 command paths
skip the zero-extent GPU operation before accessing native texture payloads.
`tests/texture_transfers.c` exercises each zero axis, destruction of either
texture, and live early releases; the resource-owner test also verifies exact
finalization counts.

The same isolated added public controls failed against pre-fix C
(`e4d95e4df227fc0d74e7b6036c7f10a0104d5437f2e6ddf53c588bccfa7b9e83`),
passed retained Zig (`3e60dbfa3281210d98639fd2f30e81ed001a31f066e39e106cd867a07b3847d1`),
and passed patched C on CPU Vulkan. The unchanged older positive transfer
fixture has a separate implicit-dimension admission difference under Zig;
that assertion was not removed or counted as passing. Buffer/texture empty
copies have a shared retained early-return behavior and are not silently
changed by this texture-to-texture parity repair. Physical execution remains
unqualified by these host/software checks.

### Empty buffer/texture copies: separate shared-correctness correction

[The receipt](EMPTY_BUFFER_TEXTURE_COPY.json) preserves a separate 12-case public
control: both C and retained Zig accepted destroyed resources on zero-extent
buffer-to-texture and texture-to-buffer submissions; pinned npm Dawn rejected
all twelve. The patched C rejects all twelve through the existing command
owner. This corrects inherited behavior rather than claiming another strict
Zig/C migration differential. Neither the retained Zig result nor an earlier
failed check is relabeled as a pass.

`test-empty-copy-submit.js` runs the same provider-neutral test with an optional
explicit provider module URL. Resource tests additionally cover all three zero
axes in both directions with live early release, destroyed endpoints, mapped
and map-pending buffers, and allocation-failure rollback/first-error retention.
Native tests exercise mapped-buffer submission as well as destroyed resources.
All backends retain their zero-GPU-operation guards. The fix preserves the
original copy validation order, exact command leases, and ordinary pending
resource finalization; it introduces no second lifetime mechanism.


### Native shader reuse and persisted target artifacts

The device now owns one bounded immutable shader cache: exact WGSL bytes,
target, enabled semantic policy and compiler identity select validated modules;
immutable module identity, selected entry, ordered override key/value bits and
Metal sample mask select executable artifacts. It retains at most sixteen
one-MiB direct allocations (plus their bounded module owners), caches successful
artifacts only, compiles outside device locks, and drops displaced owners after
unlocking. Device finalization clears the cache before closing its backend.

Selected targets also use optional local-build persistence. The shader owner
serializes pointer-free target bytes and complete typed reflection; the native
owner binds the exact loaded native, core compiler and shader images. Linux
requires mapped GNU build IDs and hashes immutable loaded spans, not paths that
may have been replaced. Missing identity, corrupt/truncated/oversized records,
I/O failure and allocation failure are misses, never compilation success or a
replacement WebGPU diagnostic. Existing bounded atomic cache-file primitives
own storage. Files are isolated from retained Zig and native-driver caches in
`$HOME/.cache/doe/c_shader_translation_cache_v1` (or
`cache/doe/c_shader_translation_cache_v1` without HOME).
`DOE_SHADER_CACHE_DIR` chooses another directory; an explicitly empty value
disables persistence. Each artifact is at most one MiB. The checksum detects
corruption; it is not authentication against an actor who can rewrite the
user's cache and digest.

`check-shader-cache` covers all three targets and exact reflection/byte reload,
policy/entry/constant/mask misses, separate device caches, corruption, malformed
references/type cycles, allocator mutation/OOM and final release.
`check-shader-cache-reload` proves a fresh-process Linux target hit without target
recompilation; the Linux no-build-ID control proves fail-closed identity. macOS
and Windows have mapped-image implementations, but ASLR-sensitive fixups can
conservatively miss; their actual cross-process hit and hardware qualification
are not established by Linux controls. Metal library call ledgers likewise do
not qualify native GPU execution.

Browser hosts can impose a device-local persistent-cache opt-out through
`doeNativeDeviceDisablePersistentCache`, declared in `doe_native_device_policy.h`.
Call it before publishing a newly created device or creating its pipelines. A
successful call permanently suppresses Doe-owned shader-cache reads/writes and
rejects subsequent private driver-cache configuration on that device. In-memory
module/stage caches remain enabled. The existing device mutex serializes this
one-way choice against the first persistent operation, including an operation
whose disk access is still in flight. Prior use or a destroyed/null device
returns `WGPUStatus_Error`; repeating a successful opt-out remains successful.
The function does not mutate process environment, deliver callbacks, or appear
in the standard WebGPU procedure inventory. Independent caller-owned cache
objects and opaque OS/driver caches have separate owners.

`check-persistent-cache` exercises actual native devices, module/stage memory
reuse, late and destroyed-device rejection, private configuration rejection,
32 disable-versus-first-use races and exactly-once loss delivery after rejected
policy cleanup. Linux `check-persistent-cache-disk` additionally wraps and
forwards the actual disk lookup/store boundary: a disabled device performs
neither operation, while the default device is a positive disk-access control.
These development controls do not qualify an entire Chromium browser or release.

An embedder that persists bytes itself, such as a browser writing from outside
its GPU sandbox, can attach a pipeline blob store to a Vulkan device with
`doeNativeDeviceConfigurePipelineBlobStore` (`doe_native_pipeline_cache.h`).
This is a caller-owned cache: the opt-out above does not suppress it, and Doe
opens no file for it. Each new compute pipeline looks up a 32-byte key bound to
the WGSL source, entry point and constants, the loaded runtime and compiler ELF
build IDs, the exact SPIR-V and the adapter/driver identity, then seeds a
temporary driver cache with the returned envelope. The envelope's device,
driver, version and digest checks and the driver's own validation make a wrong
or corrupt value a miss; when nothing compatible was loaded the new envelope is
handed back to `store`. Both callbacks run on the creating thread with no Doe
lock held. The store is set once per device and excludes the directory cache.
Render pipelines do not use it yet. `check-pipeline-blob` exercises miss/store,
in-device reuse, a driver-accepted load on a fresh device, a corrupt value,
unlocked callbacks and exact dispatch output; it does not qualify a browser.

Pinned bind-group layout entries accept `bindingArraySize` zero (default) or one
(normalized scalar). Both produce one canonical resource entry, so explicit
layouts and groups remain structurally compatible. Counts above one still
reject; this does not add binding-array shader semantics. `singleton-layout`
checks all four ordinary binding kinds, larger-count failures, explicit storage
pipeline execution and readback with an ended pass retained across submission.

For D3D12 selected compute, explicit compiler ABI19 profile501 chooses the staged
official FXC module beside the provider. Uncertified legacy artifacts, native
source imports and graphics retain DXC600/620. The existing shader cache uses the
shader owner's separate profile envelope for HLSL; fresh, memory and disk hits
retain the same immutable producer requirement. Legacy artifacts remain readable
without gaining certification. Cache identity, opt-out, lifetime, compiler entry
indices and all native resource/output gates remain with their existing owners.
