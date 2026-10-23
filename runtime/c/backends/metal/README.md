# Experimental C Metal backend

This is a real, default-off native implementation in plain C. It dynamically
loads the macOS Metal/Foundation/Objective-C system libraries and invokes typed
Objective-C runtime methods. It does not compile/link Zig, embed an interpreter,
load an alternative backend or manufacture a successful device on other hosts.
No SDKs or dependencies were installed for this implementation.

## Implemented boundary

`include/doe_metal.h`, `include/doe_metal_texture.h` and
`include/doe_metal_render.h` expose a backend-private ABI for the shared
native/resource owner to adopt, not a replacement WebGPU API or reference authority:

- Owned default device, queue, shared/private buffers and compute pipelines.
- Exact-length UTF-8 MSL compilation, named entrypoint and compiler-resolved
  workgroup size; NSError code/message survives native compilation failure.
- Sparse buffer slots, nonzero offsets and bound byte sizes. The optional
  compiler-selected size slot receives the retained checked bridge's packed
  uint32 lengths. Missing slots are explicitly cleared. No guessed workgroups.
- Direct/repeated threadgroup dispatch, buffer-region copy and byte-pattern fill.
  Every native operation uses the real Metal encoder and ends it before the next.
- Private sample1 1D/2D/array/cube/3D images, explicit mip/layer views and swizzles,
  retained samplers, staging-buffer upload/readback and texture-region copies.
  43 uncompressed color layouts and 14 BC layouts are admitted; BC requires the
  native capability property. Layout facts are not format-feature qualification.
- Mixed compute uses the same dispatch and lifetime owner with separate sparse
  buffer, texture (128 slots) and sampler (16 slots) index spaces.
- Real MSL vertex/fragment pipeline creation, explicit per-stage resource slots,
  vertex layouts, per-target blending/write masks and depth/stencil state.
- Whole render passes with color/depth/stencil attachments, native load/clear/store,
  indexed/nonindexed and instanced direct draws, viewport/scissor, cull/winding,
  blend constants, depth bias and stencil reference. Clear-only passes are real
  native encodes. Pure Depth16/Depth32 and separate Stencil8 images are admitted.
- One-shot submit, nonblocking poll, explicitly blocking native wait, terminal
  error and unknown-result handling, and deterministic owned cleanup.

The intended loader target is 64-bit macOS, x86_64 or arm64. Other OS/architecture
builds explicitly return `DOE_MTL_UNSUPPORTED`. Missing system libraries/symbols
return `DOE_MTL_LOADER_UNAVAILABLE`; absent device/limit methods fail explicitly.
This is an implementation target, not a qualified support tuple.

## Lifetime and composition

Calls for a context and its objects require caller serialization. Constructors
publish only on success. Resource/pipeline/command objects retain their creating
context, so the Objective-C runtime cannot unload while those objects live.

Metal command buffers deliberately use `commandBufferWithUnretainedReferences`,
as the retained owner does. Recording retains actual backend resources and pipelines before encoding. Native handles returned by accessors are borrowed and
must not be released, mutated or submitted independently. A shared resource
wrapper may adopt the backend pointer and finalize it using its matching release
function. The wrapper continues to own public destroy/map/usage policy.

A buffer or texture has one unresolved recording owner. Competing commands and host reads or
writes return BUSY until that recording is discarded or terminally observed.
Unsubmitted native recordings are discarded before their resource leases. A
recording failure poisons the entire recording, so earlier encoded operations
cannot accidentally submit as partial success. This conservative boundary does
not yet admit concurrent recorded use of one allocation or tuned streaming reuse.

Shared allocations start zeroed. Private allocations require a full fill/copy
before reads/bindings; initialization is speculative inside a command and is
published only on successful completion. Terminal GPU failure poisons all
participating contents. A shared allocation recovers only with a full host write;
a private allocation is replaced. Partial writes do not make failed output safe.

Private textures require full initialization of each selected mip/layer before
sampling, storage use or partial updates. A 3D mip is one whole volume; array
layers are separate subresources. Views retain their parent texture. A discarded
recording drops speculative initialization; only native success publishes it.
Texture error poisons the allocation, which must be replaced. The owner does not
infer complete storage writes from shader text or dispatch dimensions.

Texture staging row pitches are conservatively 256-byte aligned. Region origins
and interior extents respect format blocks; final edge blocks may be partial.
Array copies advance buffer image strides but pass zero native bytesPerImage for
each 2D slice, while volume copies pass the 3D stride. Padding is neither copied
nor declared initialized. Texture copies require identical formats. View format
reinterpretation is limited to opt-in linear/sRGB pairs; unsupported formats and
out-of-range layouts fail before native calls. Native driver creation may still
reject a format/usage combination. Upstream admission remains responsible for
shader access, filtering, sample type and per-device format feature compatibility.

Render pass validation checks every attachment and draw before native encoding;
then it leases pipelines, attachment views/parents, buffers, textures and samplers
under the existing command owner. No open encoder or second completion policy is
exposed. Every draw binds an explicit vertex/fragment snapshot, including clearing
absent high slots. Pipeline construction shares the existing exact-span MSL
function builder; render and compute share buffer/image binding checks.

Attachment LOAD requires initialized contents. CLEAR plus STORE establishes the
whole selected mip/layer; STORE after LOAD preserves initialization. Either load
or store DISCARD leaves contents uninitialized, regardless of presumed raster
coverage. Combined depth/stencil formats and aliased depth/stencil subresources
are rejected because the bitmap is per subresource, not per aspect. Depth/stencil
images currently support render/sample use, not storage or staging transfers.

Vertex attribute layouts and direct/instance fetch ranges are checked. Indexed
buffer byte ranges and alignment are checked, but index-value-dependent vertex
fetch safety remains an explicit upstream admission obligation. This layer does
not scan GPU indices, infer shader bounds, or treat call-ledger tests as rendering.

Only native Completed or Error status retires obligations. Pending/unknown
command release returns BUSY without consuming the handle. Callers must keep the
command, observe again, and release it after terminal status. A blocking wait
returning nonterminal status remains unknown. No timeout/cancel success is
invented; shared async/future owners may build operational deadlines over poll.

## Scope still outside this increment

Combined depth/stencil, multisampling/resolve, ETC/ASTC images, surfaces, timestamps,
external textures, shared-event notifications, async callbacks, library/binary
caches, indirect-count validation, streamed encoder reuse, prepared-program
caches and full native/WebGPU consumer wiring remain in their retained owners.
Indirect dispatch explicitly returns UNSUPPORTED; indirect draws are not exposed. Compiler/native owners must
admit shader features, usage, exact layout and robust access before this API;
it is not a standalone validation boundary for untrusted MSL. No exception bridge
is introduced: nil/NSError/status failures are handled, but Objective-C language
exceptions are not caught by this plain-C adapter.

## ABI provenance

`ABI_PROVENANCE.json` binds the retained bridge/native compute/completion sources
and render sources plus primary Apple references used for selectors, storage-mode constants,
Objective-C function-pointer calls, ownership and threadgroup semantics.
`src/objc_abi.h` contains only explicit method signatures and MTLSize/NSRange
records plus the explicit four-byte swizzle record. x86_64 uses the struct-return dispatcher for the three-word device
threadgroup limit; arm64 uses ordinary `objc_msgSend`. No speculative opaque
structure storage or C++ runtime is involved.

The host compiles both source branches and tests exact typed call arguments,
including sparse identities, offsets, size-table bytes, dimensions and native
error codes. Those checks do not validate against Apple SDK declarations or
exercise Apple's Objective-C dispatcher or rasterizer. Apple compilation, SDK ABI comparison
and real device compute/readback remain required before native acceptance.

## Focused checks

From this directory:

```
make all check abi-compile probe
make sanitize
```

Artifacts default to `/tmp/doe-c-metal`. `check` runs the owned native-call fault
and lifetime controls plus typed message-call controls. It does not interpret or
execute a shader. On Linux the real public constructor's probe must reject Metal.
`abi-compile` is syntax/type compilation of source branches, not cross-compilation
or SDK verification. No target binary is executed under forged host macros.

In this hosted environment LeakSanitizer fails under ptrace. The initial failure
is retained in `TERMINAL_RECEIPT.txt`; `ASAN_OPTIONS=detect_leaks=0 make sanitize`
passes AddressSanitizer and UndefinedBehaviorSanitizer with leak scanning disabled.
Owned native allocations/releases are still checked by the call ledger. Run leak
scanning in a supported environment before qualification.

`RENDER_SOURCE_INPUTS.sha256` binds the current sources/tests/build/documents
exercised by `DEVELOPER_RECEIPT_RENDER.json`. Verify with
`sha256sum -c runtime/c/backends/metal/RENDER_SOURCE_INPUTS.sha256` from the repository
root. Image evidence (`IMAGE_SOURCE_INPUTS.sha256`, `DEVELOPER_RECEIPT_IMAGES.json`,
`IMAGE_TERMINAL_RECEIPT.txt`) is preserved unchanged and binds commit `f6cd88b1`,
not the changed graphics-capable sources. The earlier `SOURCE_INPUTS.sha256`, `DEVELOPER_RECEIPT.json` and
`TERMINAL_RECEIPT.txt` are preserved historical compute-slice evidence, bound to
commit `33bf0b04`, not to the changed image-capable sources. Receipt consumer: the C migration lead's integration decision,
not physical support or performance promotion. No source-reduction or speedup
claim is made; complete retained delivery burden is not measured here.
