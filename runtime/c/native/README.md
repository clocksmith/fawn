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
  'comparison' entries. textureLoad and dimension queries execute; WGSL
  textureSampleCompare/textureSampleCompareLevel and depth textureSampleLevel
  are absent from the C compiler, so comparison sampling is not yet possible.
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
- A rejected BeginRenderPass/BeginComputePass still returns a pass (WebGPU invalid
  pass): it records nothing, and its commands, End and the encoder's Finish
  report the rejection.
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

- Each published device owns one progress worker and an ordinary device lease.
  It polls the existing pending list without waiting for queue idle. Unknown
  completion retains every submission/resource lease even after all public
  handles disappear; terminal success or failure retires them. The first retired
  failure remains observable by a later public operation. An idle last release
  joins the worker before returning. Startup failure publishes no device.
- Background execution pins one provider loader reference until process exit.
  This keeps provider and dependency code valid after a caller's `dlclose`; it
  retains no GPU/device resource after terminal retirement. A statically linked
  main executable already has this code lifetime. Acquire the loader reference
  outside device/resource locks, before starting a worker. Repeatedly loading
  distinct provider images therefore retains each image's code until exit.
- `make check-provider-pin` exercises real unload versus retained worker code on
  Linux/macOS without a GPU. Linux `pending-progress` uses actual CPU Vulkan work
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
- WSI enablement is explicit: `compatibleSurface` in `wgpuInstanceRequestAdapter`
  opens the adapter's Vulkan context with exactly that surface's platform bit and
  the backend's complete WSI/maintenance1 chain, then creates and releases a probe
  native surface to prove the selected queue can present to that window. Missing
  support is `Unavailable`, never a fallback adapter or headless image. Devices
  inherit the adapter's options, so their contexts carry the same WSI chain.
  `GetCapabilities` returns `WGPUStatus_Error` for an adapter not opened with that
  platform; `Configure` with such a device is a Validation error. Metal adapters
  reject any compatibleSurface.
- The native `doe_vk_surface` is created at Configure on the configured device's
  own context (adapter contexts only host short-lived capability probes, serialized
  by an adapter mutex). Configure first unconfigures any previous configuration.
- Capabilities advertise RenderAttachment|CopySrc|CopyDst (the exercised subset),
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
  reported by the loss owner), BUSY (previous frame texture/views still held) ->
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
- One frame is in flight. Callers must release the previous frame's texture and
  views before the next GetCurrentTexture or Configure (the backend refuses
  acquisition while old leases exist). Unconfigure and final surface release defer
  native swapchain retirement until those leases drop; the frame payload's surface
  lease keeps the native owner alive. A presentation-fence wait that times out keeps
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

## Optional device features

`src/features.c` is the one owner of optional WebGPU device features. The adapter
reports a feature only from what the selected Vulkan device really offers: the
canonical timestamp normalizer for `timestamp-query`; linear filtering of
r32/rg32/rgba32 float for `float32-filterable`; blending of those formats for
`float32-blendable`; D32S8 depth/stencil attachment, sampling and transfer for
`depth32float-stencil8`; enabled native drawIndirectFirstInstance for
`indirect-first-instance` and depthClamp for `depth-clip-control`. A device
request naming an unreported feature fails.
The device keeps the requested set as `NATIVE_FEATURE` bits and every gated use
checks that set, never the adapter facts: feature formats map only when enabled
(textures, views, pipeline targets, bundles and layouts all reject
depth32float-stencil8 otherwise); blending an r32/rg32/rgba32 float target rejects
without float32-blendable; and without float32-filterable those views keep the
'unfilterable-float' sample type, so a 'float' (sampler-using) layout rejects them.
Indirect draws admit a GPU-produced nonzero firstInstance only with
indirect-first-instance (otherwise the guard keeps them no-ops; direct draws
always honor firstInstance), and unclippedDepth rejects without
depth-clip-control; with it the pipeline clamps depth instead of clipping.

`tests/format_features.c` reads back exact results on a device that enabled every
offered feature: linear midpoints of r32float/rgba32float values that no 16-bit
format holds (1000001.0), two additive draws onto a 1000000.0 r32float clear
(1000007.0) and depth 0.25/stencil 7 per aspect of depth32float-stencil8. On a
device without them it checks each rejection and that an unoffered feature
fails the request. `tests/draw_features.c` reads back exact r32uint values
a*1000+instance_index for GPU firstInstance 2/1 through indirect, indexed indirect
and bundle draws (an instance-buffer overrun stays a no-op; without the feature
all stay no-ops while a direct firstInstance draws), and unclippedDepth geometry at
z 1.5/-0.5 stored as depth 1.0/0.0 where the clipped control draws nothing. A
feature the adapter does not offer prints SKIP with its reason.

## WGSL language features and texture binding view dimension

`src/features.c` owns the reported WGSL language features
(`wgpuInstanceGetWGSLLanguageFeatures`, `wgpuInstanceHasWGSLLanguageFeature`,
`wgpuSupportedWGSLLanguageFeaturesFreeMembers`) as one fixed switch beside the
instance capability exports; the accepted instance owner is unchanged. A feature
is reported only when `tests/wgsl_language_features.c` shows its probe compiles
through the public shader-module path and creates a real Vulkan compute pipeline
both without and with `requires <name>;`. Every probe also has a control shader of
the same shape without the feature that must create a pipeline, so a rejection is
attributable to the feature. Each unreported probe must be rejected at the module
or pipeline boundary, so the table cannot silently drift from the compiler. The
current table reports none; observed on lavapipe and Radeon 8060S/RADV:

| Feature | Without `requires` | With `requires` | First diagnostic |
| --- | --- | --- | --- |
| readonly_and_readwrite_storage_textures | pipeline created | module rejected | unsupported enable or language extension |
| packed_4x8_integer_dot_product | module rejected | module rejected | unsupported type name |
| unrestricted_pointer_parameters | module rejected | module rejected | only function-address pointer types are currently admitted |
| pointer_composite_access | module rejected | module rejected | array value has no named member |
| uniform_buffer_standard_layout | module rejected | module rejected | uniform type violates conservative alignment or spacing |
| subgroup_id | pipeline rejected | module rejected | compute shader entry/capability admission failed |
| texture_and_sampler_let | module rejected | module rejected | local variable type must be concrete and constructible |
| subgroup_uniformity | pipeline rejected | pipeline rejected | compute shader entry/capability admission failed |
| texture_formats_tier1 | module rejected | module rejected | unsupported storage texture format |
| linear_indexing | module rejected | module rejected | unsupported compute input builtin |
| immediate_address_space | module rejected | module rejected | unsupported module variable address space |
| buffer_view | module rejected | module rejected | expected ASCII WGSL identifier |

The compiler's `requires` directive admits only `subgroup_uniformity`, and no device
here admits `enable subgroups`, so the subgroup features cannot create pipelines.
Read-only/read-write storage textures already compile and create pipelines; that
feature becomes reportable once the compiler admits its `requires` directive (and an
executed read/write image check is added). The `buffer_view` probe uses the
proposal's `buffer<N>`/`bufferView` spelling; it is rejected at parsing.
An empty list owns no storage (`features` is NULL); NULL instance or output is safe.

`wgpuTextureGetTextureBindingViewDimension` follows core-mode semantics. The pinned
header requires accepting a `WGPUTextureBindingViewDimension` chain on
`WGPUTextureDescriptor` and ignores its contents on core devices. This provider has
no compatibility-mode view restrictions, so it accepts exactly that one chain node,
ignores the value, and the getter returns `WGPUTextureViewDimension_Undefined` for
every texture, as the reference core implementation does. Unknown or repeated chain
nodes still reject. The provider does not yet advertise `CoreFeaturesAndLimits`;
that separate claim needs complete core limits and is not made here.

## Storage textures

The resource owner's `resource_storage_format` table is the WebGPU core storage
format set: rgba8unorm/snorm/uint/sint, rgba16uint/sint/float, r32uint/sint/float,
rg32uint/sint/float and rgba32uint/sint/float. All admit write-only and read-only
access; read-write is limited to the three r32 formats. Layouts with other formats
or read-write on other formats are Validation errors, as is StorageBinding usage on
a non-storage format; storage bindings need StorageBinding usage, one mip and the
exact format. bgra8unorm storage (bgra8unorm-storage) is not offered: the C compiler
has no bgra8unorm storage format. The compiler's sixteen storage formats map to
these names; 1D, 2D, 2D-array and 3D storage views are admitted. Fragment-stage
writes (write-only/read-write) use the device's enabled fragmentStoresAndAtomics;
vertex-stage writes stay rejected. Default (automatic) layouts report
`unfilterable-float` for f32 textures when the pipeline declares no sampler, per
the WebGPU default-layout rule; with samplers the conservative `float` remains
because the compiler does not reflect exact texture/sampler pairing.

`tests/storage_textures.c` writes all sixteen formats with textureStore and checks
every channel of a 4x4 readback exactly; reads r32uint/sint/float through read-only
storage and increments them in place through read-write storage; writes 1D,
2D-array and 3D views; stores from a fragment shader at each pixel; and repeats
write-then-sample across passes in two submissions. Negatives cover StorageBinding
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
`maxColorAttachmentBytesPerSample` reports the WebGPU baseline 32 (Vulkan has no
such per-sample byte limit); both feed resource admission. All slots lower into the
backend's one native pass span. The C compiler already emits and reflects multiple
`@location` fragment outputs (struct outputs; spirv-val clean for Vulkan 1.2).

Sampler `maxAnisotropy` has no upper bound at admission; values above one require
all filters linear, use the device's enabled `samplerAnisotropy`, and clamp to its
maximum. A device without that feature rejects anisotropic samplers instead of
silently using one (the spec would also permit clamping to 1). The C compiler only
admits explicit-LOD `textureSampleLevel` (implicit `textureSample`, gradient and bias
sampling are rejected), so derivative-driven anisotropic footprints cannot be
exercised yet; the public check proves admission and a working anisotropic sampler.

`tests/mrt.c` renders a sparse four-slot layout at exactly 32 bytes per sample
(RGBA8Unorm additive blend, unused slot, RGBA32Uint, RGBA8Unorm with a green/alpha
write mask) directly and through a sparse-format render bundle, resolves two 4x
slots with discarded sources, and checks exact half-split pixels in every target.
Negatives cover slot count, the byte budget, a subresource in two slots, only-unused
slots, missing/mismatched outputs, blend on an unused slot and a packed pipeline
against a sparse pass. `tests/anisotropy.c` checks zero/non-linear rejection, an
above-maximum clamp and a 16x anisotropic explicit-LOD draw whose 1:1 texel-center
oracle is exact for every filter; it is not a filtering-quality claim.

## Intentionally unprovided or rejected

Vulkan indirect dispatch and nonindexed/indexed indirect
pass/bundle draws use real GPU guards; Metal graphics, indirect and indexed draws remain uncomposed.
Graphics
admits up to eight sparse color slots and/or depth/stencil, sample counts 1 and 4, direct
triangle/line lists or strips and every pinned WebGPU vertex format (8/16/32-bit
integer and normalized, float16/32, unorm10-10-10-2, unorm8x4-bgra) with offsets
aligned to min(4, format size).
PointList without device maintenance5, dual-source blending,
vertex-stage storage writes (fragment writes need enabled fragmentStoresAndAtomics)
and fragment FragDepth/SampleIndex/SampleMask
remain explicitly rejected. WGSL discard requires actual enabled demote support;
unsupported devices reject that pipeline. Blend constants must be finite f32.
Texture format reinterpretation beyond srgb/unorm viewFormats pairs (RGBA8,
BGRA8; resource-validated, then a mutable Vulkan image with a format list), srgb
viewFormats together with StorageBinding, depth storage, depth comparison sampling (compiler), multisampled
buffer copies/storage, and cube arrays without device imageCubeArray are rejected.
The baseline admits 23 named Vulkan
color formats, subject to actual device usage/format capabilities. Device
descriptors may request the optional features `src/features.c` reports for the
actual adapter (see Optional device features). Other required features and required limits
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
Metal image/graphics/timestamp composition, Metal/Windows/Android surfaces, D3D12
admission and package adoption remain unfinished. Private backend checks do not qualify those public
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
PASS public view formats: rgba8unorm rendered through its srgb view stores exact sRGB codes 64/128/200/255 (copy and unorm-view textureLoad), rgba8unorm-srgb rendered through its unorm view stores raw bytes, bgra8unorm srgb view keeps byte order, non-pair/unlisted/pipeline-mismatch controls and the srgb-storage limitation, early releases; exact 96 bytes and 4 decoded codes (CPU Vulkan only)
PASS public cube arrays: enabled imageCubeArray, two uniformly filled cubes sampled along all six face axes per array index via texture_cube_array/textureSampleLevel, textureNumLayers, automatic CubeArray layout, partial/cube-dimension view and storage-layout controls, early releases; exact 12 face values and 1 layer count (CPU Vulkan only)
PASS public depth/stencil sampling: rendered depth32float and depth24plus-stencil8 DepthOnly maps via texture_depth_2d (automatic depth layout) and texture_2d<f32> (explicit unfilterable-float), StencilOnly as texture_2d<u32>, two-layer texture_depth_2d_array, texture_depth_cube dimensions, comparison-sampler binding admission, 7 view/binding/creation controls, early releases; exact 48 depth values, 8 stencil values and 2 dimensions (no comparison sampling: compiler lacks the builtins) (CPU Vulkan only)
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
PASS public format features: 3 of 3 offered and enabled (float32-filterable linear r32float/rgba32float midpoints exact, float32-blendable additive r32float 1000007.0, depth32float-stencil8 depth 0.25/stencil 7 per aspect); without them depth32float-stencil8 texture/pipeline/bundle, r32float blending and 'float' r32float binding reject, an unoffered feature fails the request (CPU Vulkan only)
PASS public draw features: 2 of 2 offered and enabled (indirect-first-instance: GPU firstInstance 2/1 reaches instance attributes and instance_index exactly for indirect, indexed indirect and bundle draws, an instance-buffer overrun stays a no-op; depth-clip-control: unclippedDepth draws z 1.5/-0.5 clamped to depth 1.0/0.0 where the clipped control draws nothing); without them nonzero indirect firstInstance is a no-op while direct firstInstance draws, and unclippedDepth rejects (CPU Vulkan only)
PASS public copy layouts: undefined rowsPerImage for one image and undefined bytesPerRow for one row through queue writes, buffer-to-texture and texture-to-buffer copies with exact texels, two-image and two-row undefined-stride rejections (CPU Vulkan only)
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

## Doe compute-program extension

`include/doe_native_programs.h` declares the doe-gpu native program contract the
retained Zig provider exports, listed separately in `EXTENSION_SYMBOLS.txt` (never
webgpu.h names, never resolved by `wgpuGetProcAddress`; `tests/audit-exports.py`
checks both inventories). The contract version is
`config/compute-program.schema.json` `$defs.nativeContractVersion`, read at build time.
Vulkan only; other backends reject preparation with a device error.

- `doeNativeComputeProgramPrepare` (gpu-recorded) claims a finished, never-submitted
  command buffer in the resource owner and records ONE reusable Vulkan command buffer
  (buffer copies, clears, direct dispatches, timestamp writes including compute-pass
  timestampWrites, and query resolves), sealed so preparation
  dispatches nothing and buffers stay writable before the first run.
- `doeNativeComputeProgramPrepareHostReplay` (native-recorded) claims the same way;
  every submit lowers the retained commands into a fresh one-shot native recording.
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

## Physical developer runs and holds

Do not run this on Miles or Suby while their possible disassembly/cooling hold
remains. Every adapter-using client selects its adapter through
`tests/adapter_class.h`: software (CPU fallback adapter) by default, or a
nonfallback GPU adapter with `DOE_C_TEST_ADAPTER=physical`. Each PASS line names
the class and device it ran on. `make check-physical` runs the same 36 clients on
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
`copy-layouts`) passed. That is
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
