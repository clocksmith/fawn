# Shared runtime interface inputs

`webgpu-headers/webgpu.h` is the canonical live WebGPU C interface for the C
runtime, package addon, benchmark consumers and retained Zig builds. The pin and
SHA-256 remain in [`config/webgpu-abi-source.json`](../../config/webgpu-abi-source.json).
The upstream BSD license accompanies the header. These are ordinary files on
every platform; builds do not depend on symlinks.

The byte-identical header and license at `runtime/zig/vendor/webgpu-headers/`
remain frozen reference inputs for independently sealed C migration tests,
source manifests and historical receipts. Do not redirect those seals to the
live owner or edit their recorded hashes. Current builds use this directory.

`spirv-headers/LICENSE` supplies the Khronos notice included in C native package
archives. The pinned `GLSL.std.450.h`, its generator and its reference license
remain under `runtime/zig/vendor/spirv-headers/`; this directory does not copy
or replace that compiler input.

Zig compiler reference sources, TSIR emitter inputs, bootstrap WGSL, timestamp
WGSL and explicit legacy/differential routes retain their existing owners.
