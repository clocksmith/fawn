# Experimental C D3D12 backend

This is a standalone backend-private C owner, not a production-provider switch
or a complete WebGPU backend. Actual Windows code uses the installed SDK's C
COM declarations. No retained Zig bridge is linked, invoked or removed.

## Implemented boundary

- Exact DXGI adapter ordinal, explicit software-adapter opt-in, D3D12 device,
  direct command queue, private fence and observable adapter identity.
- Default/UAV-capable, zero-initialized upload, and readback buffers; checked
  transfer ranges and scoped map/unmap. Default/readback initialization requires
  a full copy, published only after successful completion.
- Precompiled DXIL/DXBC compute pipelines, immutable structured SRV/UAV layout,
  register spaces, ranged descriptors and three-dimensional dispatch.
- Single-use command recording, early caller release, atomic submission ownership,
  finite waits, partial retirement, unknown-completion recovery and terminal loss.
- Typed statuses, first close failure, allocation rollback and native bounds.

All context calls require caller serialization. Objects keep the context busy,
not independently reference-counted: release every caller object, poll pending
submissions, then call context_destroy. BUSY means the context is still owned by
the caller and must remain available for polling/release. Submitted commands are
context-owned even if the caller releases them. Recording-only commands are
canceled by release. Different pending commands may use disjoint resources; a
buffer cannot belong to another recorded/unresolved command. This restriction is
explicit, not a claim of full command-buffer or WebGPU concurrency parity.

No texture, rendering, presentation, query, indirect dispatch, uniform-binding,
WGSL-to-DXIL compiler invocation or public WebGPU export is implemented here.
Unsupported broad surfaces are not padded out with success-looking exports.
The native frontend/compiler still owns reflection, shader admission, workgroup
limits, dynamic offset policy and bounds-proof obligations.

## Source trace and consolidation

The retained Windows path creates/retains a native runtime in
`runtime/zig/src/native/lifecycle/doe_instance_device_native.zig`. Compute flows
through `native/compute/doe_compute_d3d12.zig` into
`backend/d3d12/d3d12_bridge.c`, with queue/retirement behavior in
`d3d12_native_runtime.zig`, `d3d12_runtime_compute.zig` and
`d3d12_runtime_upload.zig`. The new owner consolidates device, ownership, commands
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

## Checks

From this directory:

    make check
    ASAN_OPTIONS=detect_leaks=0 make sanitize

These compile the portable owner with strict C11 warnings and inject failures at
its private typed native-call boundary. The mock does not execute copies or
shaders. Its read pattern tests mapping eligibility only. Allocation accounting
checks owner teardown. LeakSanitizer is unavailable under this executor's ptrace;
ASan/UBSan can run with leak detection disabled, which is not an LSAN pass.

On Windows in a configured MSVC/Windows SDK developer command prompt:

    build_windows.cmd
    dxc -T cs_6_0 -E main tests\native_compute.hlsl -Fo "%TEMP%\doe-c-d3d12\native_compute.dxil"

The build script only builds. After separate execution authorization:

    "%TEMP%\doe-c-d3d12\native_compute.exe" "%TEMP%\doe-c-d3d12\native_compute.dxil"

A Windows-target C compiler with the actual SDK may alternatively use
`make windows` and `make native-fixture`. Non-Windows `make` runs only host
checks; there is no fake production D3D12 implementation on Linux. The native
fixture releases caller upload/storage/pipeline references before submission,
then releases the caller command before polling and compares real readback.

Windows SDK compilation, DXC compilation, native driver readback, debug-layer
validation and physical qualification are **unrun** in the dot cloud environment.
The fixture's portable syntax check is not any of those qualifications.
