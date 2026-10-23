# C resource and recorded-command ownership

Status: authored and developer exercised, not independently qualified. Retained
Zig remains authoritative until integrated qualification. No production/default
cutover, physical GPU, performance or complete WebGPU parity claim is made here.

## Owner and use

`include/doe_resources.h` is the typed internal C seam;
`include/doe_resources_extended.h` adds query, external-texture and render owners.
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
- `commands.c`, `compute_pass.c`: sticky encoder admission, pass locking, transfer,
  direct/indirect compute snapshots and single-use submission self-leases.
- `queries.c`, `external_textures.c`, `render.c`: real native query-pool ownership,
  retained imported plane views, and attachment/draw/query replay snapshots.
  Render begin/end packets bracket query events, including empty query scopes.

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

Render ownership currently admits the retained one-color/optional-depth-stencil,
eight-vertex-buffer/four-group envelope, direct/indexed draws and occlusion scopes.
It does not execute graphics/query work. MRT, resolves, bundles, indirect draw,
surface presentation, binding arrays and external-texture color conversion remain
unsupported. Actual map/future delivery and native shader execution are consumer
responsibilities. Unsupported consumer command kinds must reject before submit.

## Focused checks and evidence

`make -C runtime/c/resources check` builds GCC C11/Werror into `/tmp`: 13 base and
7 extended host suites cover transactional failure/untouched outputs, first error,
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
transfer subset is established here, not native rendering/query execution.

```
ASAN_OPTIONS=detect_leaks=0 make -C runtime/c/resources \
  BUILD=/tmp/doe-c-resources-asan \
  CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread -fsanitize=address,undefined -fno-omit-frame-pointer' check
```

Address/undefined sanitizer checks pass; LeakSanitizer cannot run under this
ptrace environment. Exact fixture allocation/native-release counters reach zero,
not whole-program leak qualification. `DEVELOPER_RECEIPT_EXTENDED.json` binds the
current focused checks to unchanged source hashes, including consumed backend
bytes. `DEVELOPER_RECEIPT.json` preserves the earlier smaller historical check.
Run the common source-size check; every owned C source/header is below 999 lines.
