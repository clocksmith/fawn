# Instance capability amendment, 2026-10-25

The owner authorized advertising `TimedWaitAny` and
`MultipleDevicesPerAdapter`, conditional on verification with the C Metal
backend. This amendment supersedes the empty capability result in the original
[sealed contract](CONTRACT.md#native-waitpump-and-capability-semantics).
Original seals, independent tests and historical acceptance remain unchanged;
they describe their bound source, not this amendment's acceptance.

## Current contract

The instance owner returns one immutable, process-lifetime array containing
exactly these two pinned WebGPU feature values. `has_feature` reads that same
array. FreeMembers is a no-op. `ShaderSourceSPIRV` and unknown features remain
unsupported. No instance ABI record, allocation, lease or queue changes.

`timedWaitAnyMaxCount` is 64. A nonzero timeout with more than 64 records
returns Error before reading or changing records or allocating wait storage.
Zero-time polling retains its checked size and allocation rules. Count zero
still succeeds without reading instance or records; a nonzero count with NULL
records returns Error. Finite timeout never retires pending work. The accepted
future queue remains the only readiness, ordering and delivery owner.

The public C frontend validates required features and limits before allocation:
all requested features must appear in the owner's array; a nonempty feature
list requires storage; unknown descriptor/limit chains and a requested count
above 64 reject. NULL/default descriptors and limits at or below 64 succeed.
The retained Zig instance seam still uses its existing descriptor behavior;
its selected C queries observe this amended capability result. Production
selection is unchanged.

## Focused acceptance

- `make -C runtime/c/instance check`: finite timeout leaves a pending callback
  undelivered; readiness then delivers once through 64 duplicate wait records;
  limit overflow and NULL-record rejection preserve caller state. Existing
  allocation, lease, fault and concurrency checks remain in this gate.
- `make -C runtime/c/native check-instance-devices`: typed C++ procedure calls
  verify descriptor rejection and capability consistency. The public C client
  requires both features and creates two devices from the same adapter, checks
  independent upload/copy/readback contents, then releases the first device and
  uses the second again. Adapter, device, map and WorkDone callbacks use
  WaitAnyOnly and finite five-second waits.
- The public client ran on Linux CPU Vulkan (lavapipe) and on Apple M3 Metal,
  macOS 26.6.2, SDK 26.0, from `685128c86` plus this amendment's source changes.
  The Metal process had a 20-second deadline; both passed. This is focused
  developer evidence, not physical-backend or complete ONNX qualification.

The test selects adapters through `tests/adapter_class.h`; physical execution
requires `DOE_C_TEST_ADAPTER=physical` and the intended `DOE_C_TEST_BACKEND`.
Default execution requests software Vulkan. Preserve each backend's independent
qualification gates and existing hardware holds.
