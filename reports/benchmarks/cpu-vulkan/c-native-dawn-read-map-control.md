# Read-map audit: retain snapshot isolation

This [source-bound control](c-native-dawn-read-map-control.json) prevents an
unsafe response to the [measured transfer losses](c-native-dawn-package-diagnostic.md).
Its consumer is the C readback performance audit, not a superiority claim.

The unchanged provider-neutral
[`test-read-map-discard.js`](../../../packages/doe-gpu/test/integration/test-read-map-discard.js)
writes `[42,43,44,45]`, maps READ, changes the first returned word to 999, unmaps,
then maps READ again. It also checks detachment of both returned ArrayBuffers.
The [WebGPU READ contract](https://gpuweb.github.io/gpuweb/#dom-gpumapmode-read)
requires local modifications to be discarded, including for later mappings.

Observed on the same CPU Vulkan driver and Node 24.22.1:

| Hashed provider | Remapped values | Exit | Detachment |
| --- | --- | ---: | --- |
| C native | `[42,43,44,45]` | 0 | Both detached |
| Retained Zig | `[999,43,44,45]` | 1 | Both detached |
| npm Dawn 0.4.0 | `[999,43,44,45]` | 1 | Both detached |

This is a narrow C correctness improvement relative to these two artifacts,
not evidence of complete parity or general Dawn invalidity. The original
performance plans do not mutate READ mappings: their slower C timings remain
valid observations and are not erased by this additional control.

## Copies, allocations and synchronization

The C path is explicit and source-traced:

1. `native/src/backend_buffers.c` creates two host-visible Vulkan allocations
   for the exact transfer plan. `backends/vulkan/src/resources.c` requires
   HOST_VISIBLE|HOST_COHERENT, maps each allocation persistently, and zeroes its
   logical extent. For this plan, initialization writes 2 MiB in total.
2. The public queue write copies the 1 MiB input into the source allocation;
   the encoded buffer copy transfers 1 MiB into the readback allocation.
3. `native/src/buffers.c:wgpuBufferMapAsync` allocates a 1 MiB mapping snapshot,
   settles dependent buffer uses, then `native_buffer_read` copies 1 MiB from
   the coherent backing allocation. The callback/resource owners protect the
   allocation through completion, cancellation and public release.
4. Node addon `doe_buffer_get_mapped_range` normally creates an external
   ArrayBuffer over this snapshot without copying. A host which prohibits
   external buffers uses the explicit owned-copy fallback; that branch is not
   established by this ordinary Node run.
5. Forced `mapAsync-host-copy` takes one additional 1 MiB JavaScript slice for
   capture, on both providers. C detaches returned ranges and frees its
   snapshot on unmap; READ changes are never written back to native memory.

Thus C's ordinary readback uses a snapshot allocation/copy and a capture
allocation/copy. Returning the persistent Vulkan allocation directly would
violate the mutation-discard control. No safe redundant copy was established.
The npm Dawn binary's internal allocation count is **not known** from outward
API timing; the observed mutation leak does not prove its entire implementation.

The addon optionally invokes `doeNativeQueueFlush` before mapping, but this C
artifact does not export that legacy function. Its pointer is null. C instead
polls completion and waits only while the mapped Vulkan buffer has recorded
pending uses. No second blocking global flush was found in this path.

From the already retained 16-sample exact-1-MiB round (C / Dawn median ms):

- Buffer creation: 1.526 / 0.107.
- Encoder finish: 0.400 / 0.017; queue submit: 0.568 / 0.088.
- Map: 0.573 / 0.472; get mapped range: 0.196 / 0.020.
- Capture copy: 0.301 / 0.371; unmap: 0.175 / 0.020.

The mapping delta is about 0.10 ms, not the entire operation loss. Eager buffer
allocation/zero-initialization and command construction remain larger measured
questions. Lazy initialization would require a proved shared range-initialization
owner and unchanged zero/readback/lifetime oracles; it is not implemented here.

## Reproduce

Use the same scoped native-library/ICD selectors described by the campaign.
Set `MESA_SHADER_CACHE_DISABLE=true`, `LP_NUM_THREADS=1`, and writable HOME.
No shader, timing threshold or output tolerance is involved:

```sh
node packages/doe-gpu/test/integration/test-read-map-discard.js
node packages/doe-gpu/test/integration/test-read-map-discard.js "$DAWN_MODULE_URL"
```

The default module is Doe; `DOE_WEBGPU_LIB` selects the exact C or retained Zig
library. An explicit module URL selects the independent comparator. Adapter
absence is a failure, not a skip. Preserve the comparator's nonzero result.
Physical GPU, browser and other npm Dawn versions remain untested by this fixture.
