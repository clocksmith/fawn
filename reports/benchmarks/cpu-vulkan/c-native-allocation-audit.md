# Vulkan allocation audit: no cold-pool speed claim

Consumer: deciding whether a zero-preserving Vulkan buffer pool is justified
by the [retained CPU package losses](c-native-dawn-package-diagnostic.md).
This is a diagnostic rejection of that optimization hypothesis for these
three cold-process plans, not a claim that pooling never helps another workload.
No allocator implementation or initialization contract changed.

## Actual calls on unchanged plans

A process-local Linux loader interposer observed the C backend's typed
`vkCreateBuffer`, `vkGetBufferMemoryRequirements`, `vkAllocateMemory`, and
`vkMapMemory` calls. A mapped-pointer-filtered `memset` interceptor separately
observed initial zero fills. Each of the original three plans still completed
its exact oracle with zero errors/unsupported/skipped rows. Interposed timings
are profiling observations, not replacements for the canonical 16-sample data.

| Plan | Logical buffer sizes, bytes | Creates / allocations / maps / initial zero fills |
| --- | --- | --- |
| Queue completion | 4, 4, 4, 4 | 4 / 4 / 4 / 4 |
| Eight pipelines | 32, 32, 4, 4 | 4 / 4 / 4 / 4 |
| Exact 1 MiB transfer | 1048576, 1048576, 4, 4 | 4 / 4 / 4 / 4 |

The first two buffers belong to each workload; the extra four-byte pair is the
executor's completion-fence control. Thus the earlier read-map audit's two
workload allocations are **four total allocations**, with eight additional
initialized bytes. All observed requirements had alignment 64, memory type bits
1, and requirement size equal to logical size; allocations used memory type 0.
No general driver/alignment guarantee follows from these observations.

One duration-profiling observation per plan (summed milliseconds):

| Plan | Create calls | Allocation calls | Map calls | Initial zero fills |
| --- | ---: | ---: | ---: | ---: |
| Queue | 0.009634 | 0.040672 | 0.000571 | 0.016273 |
| Pipelines | 0.008603 | 0.038769 | 0.000441 | 0.017397 |
| Transfer | 0.009394 | 0.046018 | 0.000460 | 0.741962 |

Transfer zero-fill observations were 0.360205 and 0.374987 ms for the two 1 MiB
buffers, plus 0.003255 and 0.003515 ms for the two four-byte buffers. Logging and
instrumentation perturb execution; these are localization evidence only.
The native object code also contains the explicit initialization `memset` call.
The ordinary source path requires HOST_VISIBLE|HOST_COHERENT memory, maps the
allocation once, and zeroes every logical byte before publishing the buffer.

The same exact plan profiled through the existing executor worker with V8 CPU
sampling showed approximately 0.97 ms in the native `deviceCreateBuffer`
boundary. The ordinary 16-sample phase median was 1.526 ms for buffer creation.
These measurements are different scopes/runs; they must not be subtracted as
an exact accounting identity. They consistently locate much more cost in
initialization/first touch than in allocation or map API calls.

## Conservative proposal and decision

A possible shared-owner design would use bounded, context-owned, buffer-only
HOST_COHERENT slabs and explicitly aligned nonoverlapping spans. Each buffer
would keep its span through existing command/pending leases; allocation and
reuse would eagerly zero the whole logical range. READ snapshots and all
map/unmap/abort semantics would remain unchanged.

Before admission, core `vkGetBufferMemoryRequirements2` and
`VkMemoryDedicatedRequirements` must establish dedicated-allocation requirements.
Required/preferred dedicated allocation, unsupported alignment, unsuitable memory
type, excessive size and pool exhaustion would take the existing dedicated
path. Device-local, noncoherent, external and image memory would remain outside
this first candidate. No noncoherent atom/alignment assumption may be inherited
from this coherent-only CPU observation. Empty retained slab memory must be
bounded and freed before the device; unknown completion must retain spans.

Required controls would include zero-on-reuse, partial writes, nonoverlap,
alignment, recorded/in-flight release, allocation/bind/map failure rollback,
context teardown, READ mutation discard and matched RSS/performance checks.

**Not implemented:** eliminating three allocation/map calls cannot eliminate
required cold zero-fill/first-touch costs, and the observed API-call budget is
only tens of microseconds. Adding slab reservation, dedicated queries and
lifetime machinery is not currently supported as a likely win for these plans.
A reused-resource workload or a driver with materially different allocation
costs could justify revisiting it, with new evidence rather than reusing this
cold-process result. Physical GPU behavior remains unqualified.

## Identity and retained boundaries

Source: `69e7a5c4e1430908829ce5a171f469ffb1521a00` (no native-code change
since the package artifact used by the campaign). Native/provider/driver hashes
and unchanged plan hashes are in the campaign receipt. The typed owner remains
`runtime/c/backends/vulkan/src/resources.c`; device teardown remains in
`loader.c`. No new allocator or parallel resource semantics were introduced.

Remaining measured questions are wrapper startup, eager initialization,
encoder construction and submission. The required READ snapshot is not a safe
zero-copy target. Original performance losses, matched MSAA correctness failure
and untested physical/browser qualification remain unchanged.
