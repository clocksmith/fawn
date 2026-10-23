# Unqualified Linux Vulkan compute handoff

This is a tiny C-only smoke candidate, not production selection or full parity.
Hardware execution remains unrun in the cloud. The existing software-ICD result
is explicitly separate. Run only in the isolated folder authorized by the user.
Do not replace installed libraries, alter ICD files, or use the Miles machine.

## Dependencies and source

Existing Linux LP64 host with Vulkan 1.2 driver/loader `libvulkan.so.1`, a hardware
compute queue and robustBufferAccess; existing GCC/C11, C++17 compiler, GNU make,
Python 3, `ar`, `timeout`, and libc/libdl. No SDK headers, downloads, installs,
network, Zig compiler or external service is required. Build the sibling selected
C WGSL compiler from the same source snapshot, not an installed stale compiler.

The implementation ABI derives only from hashes in `ABI_PROVENANCE.json`.
`SOURCE_MANIFEST.json` binds the stable owner files except itself. The lead must
also bind the exact C compiler snapshot/commit used for the end-to-end check.

## Build and inspect

From the repository root, put all output in a fresh temporary directory:

```
OUT=$(mktemp -d /tmp/doe-c-vulkan-hardware.XXXXXX)
mkdir "$OUT/compiler"
cp -R runtime/c/compiler/src runtime/c/compiler/include runtime/c/compiler/Makefile "$OUT/compiler/"
make -C "$OUT/compiler" build/doe-compiler-c
make -C runtime/c/backends/vulkan OUT="$OUT/vulkan" all check
"$OUT/compiler/build/doe-compiler-c" --shader-path runtime/c/backends/vulkan/tests/compute.wgsl --out "$OUT/compute.spv"
make -C runtime/c/backends/vulkan OUT="$OUT/vulkan" "$OUT/vulkan/native_compute"
```

Inspect the adapter identity printed by the native test and retain it with the
source/build hashes. The default picks exact enumeration ordinal zero and rejects
CPU adapters; it never silently tries another adapter. Do not pass
`--allow-software` to a hardware run. If ordinal zero is not the authorized adapter,
stop and use an explicitly reviewed selector, not an accidental different GPU.

## Bounded execution and abort

```
(ulimit -c 0; timeout --signal=TERM --kill-after=5s 60s "$OUT/vulkan/native_compute" "$OUT/compute.spv")
```

The internal fence wait is 5 seconds; external timeout bounds driver initialization
or shutdown. Timeout, driver error, device loss, unsupported adapter or nonzero
exit is not a pass. Stop on the first such result, record stderr/stdout and status,
and do not loop or benchmark. Kernel/driver process teardown, not unsafe explicit
resource destruction, handles a process that exits with unresolved GPU ownership.

Work is one 256-byte storage buffer, one 256-byte readback buffer, one compute
pipeline, two 64×1×1 dispatches with local size 1, one fill, one copy and one submit.
The SPIR-V file is capped at 16 MiB by the test; driver internal allocation is not
bounded by this test and must not be described as a measured GPU-memory budget.
No filesystem write occurs in the test, except ordinary driver-managed caches.
The test never presents, contacts a service, persists a setting or changes provider.

The independent CPU expectation is `9*i + 28` for all 64 output words. First is 28,
last is 595. A pass requires every word to match, visibility across dependent
dispatches, explicit pending-read/release rejection, and early caller release
without premature native destruction. Preserve the exact command and executable,
SPIR-V, source, adapter and driver identities with the output. One passing run is
only this tuple's smoke evidence, not complete feature, lifecycle or performance
qualification.

For software reproduction only, an already-present SwiftShader ICD may be selected
with process-local `VK_ICD_FILENAMES=/verified/path/vk_swiftshader_icd.json` and
`--allow-software`. Never create or modify system ICD configuration.
