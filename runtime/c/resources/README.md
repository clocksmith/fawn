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
  textures/views/samplers and shared checked block/mip/row/image copy geometry.
- `bindings.c`: canonical layouts, exclusive automatic-layout identity, retained
  groups/pipeline layouts, dynamic binding ranges and native pipeline leases.
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
Explicit NULL slots follow the same required-group predicate. The status-returning
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
No second native recorder, resource registry or completion engine is introduced.
Nullable vertex bindings clear only logical state after offset/size validation
against a zero-sized buffer; historical uses remain. Render/pipeline/bundle device,
layout and descriptor getters borrow existing objects without extra leases.
Occlusion indices are single-write within each pass, including empty query scopes;
a later pass may reuse them. A bounded per-pass bitmap enforces this before
command allocation, without introducing native query availability state.

Query descriptors use one allocation-free preflight for named type and count
0..4096, following current [WebGPU query creation](https://gpuweb.github.io/gpuweb/#dom-gpudevice-createqueryset)
rather than retained native's nonzero-only, unbounded count. A zero-query identity
retains its device but needs no native pool. Every write/resolve still requires an
index strictly below count; zero-count resolve at the end is invalid. Device
feature enablement and timestamp-unit normalization remain consumer obligations.

Copy admission shares only checked byte-footprint arithmetic with native Vulkan
through `../common/include/doe_copy_footprint.h`; format, pitch/default, zero extent,
alignment, capacity and native granularity policies remain in their actual owners.

Render ownership currently admits the retained one-color/optional-depth-stencil,
eight-vertex-buffer/four-group envelope, direct/indexed/indirect draws, timestamp writes and occlusion scopes.
The owner itself does not execute graphics/query work. The standalone native
consumer below exercises a narrower real graphics subset. MRT, resolves,
surface presentation, binding arrays and external-texture color conversion remain
unsupported. Actual map/future delivery and native shader execution are consumer
responsibilities. Unsupported consumer command kinds must reject before submit.

## Focused checks and evidence

`make -C runtime/c/resources check` builds GCC C11/Werror into `/tmp`: 13 base and
8 extended, 6 bundle, 6 binding-feature, 6 dispatch-usage, 9 render-usage and 1 query-admission host suites cover transactional failure/untouched outputs, first error,
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
with target `vulkan-render-lifetime`. One self-contained bundle is replayed twice
under distinct dynamic scissors within one real native pass, producing 48 magenta
and 16 blue pixels in an 8x8 target. A following empty LOAD/STORE pass preserves
the image. A bounded borrowed draw span lowers the existing immutable packets;
missing end and unsupported interleaved query events reject before native work. All caller resources and the bundle are released before native
recording; pending native reads/releases reject, and all six adopted payloads
finalize only at terminal resource settlement. Native commands retain exact
physical objects through later readback and retirement. Shader fixtures are
retained Tint `particles.wgsl` vertex/fragment SPIR-V, not C graphics compiler
output; their hashes/provenance are in the backend `SMOKE_IMAGE_FIXTURES.json`.
The focused consumer admits two direct draws, one RGBA8 target, one uniform
binding and one vertex buffer;
unsupported depth/query/indexed/indirect/discard/dynamic blend state rejects.
This is software execution and ownership evidence, not public native render ABI
integration, graphics parity or physical GPU qualification.

The `vulkan-query-lifetime` target uses the same explicit software ICD and timeout.
It consumes timed-pass begin/end and command timestamp records through the native
query availability/reset owner, resolves five packed raw device ticks with two
unwritten entries zeroed by native commands, and verifies all neighboring bytes.
No CPU-generated query values or second availability map are used. Public
256-byte resolve alignment remains stricter than the native packed8-byte seam;
CPU reads remain unavailable until actual native completion. Raw ticks are not a
portable WebGPU nanosecond result: the retained GPU normalization shader/calibration
path still needs integration before public timestamp semantics are complete.

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
`DEVELOPER_RECEIPT.json` preserves the earlier smaller historical check.
Run the common source-size check; every owned C source/header is below 999 lines.
