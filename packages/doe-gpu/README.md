# doe-gpu

`doe-gpu` runs WebGPU outside the browser with **Doe**, a native WebGPU
implementation written in C. One package gives Node, Bun and Electron (Deno is
experimental) the standard WebGPU API, plus a WGSL compiler that targets
SPIR-V, Metal and HLSL/DXIL.

Doe is the GPU component of [Fawn](https://github.com/clocksmith/fawn). It does
not need the Fawn browser.

## Goals

- **Faster than Dawn on unchanged WebGPU code.** Same application, same
  shaders, lower latency, at p50 and p95.
- **One runtime everywhere JavaScript runs.** Node, Bun, Deno and Electron get
  the same API and the same behavior.
- **Portable, inspectable GPU programs.** The WGSL compiler is usable on its
  own, and every run can report exactly which runtime and backend executed it.

## What it does well

- **Drop-in for the `webgpu` npm package.** `doe-gpu/node-webgpu-loader`
  substitutes Doe under existing `webgpu` imports without code changes.
- **Never falls back silently.** If the requested GPU provider isn't there, it
  says so. It doesn't quietly run on another GPU or the CPU.
- **Small, explicit compute helper.** `gpu.bind(device).compute(...)` runs a
  kernel and cleans up its own buffers.
- **Receipts.** A run can emit the exact runtime, backend and output identity,
  so results are reproducible and comparable.

## Performance highlights

Measured on the C runtime this package ships, against the `webgpu` npm package
(Dawn) on the same machine: Gemma 3 inference (64 prompt + 64 generated tokens)
in Node, 270M unless noted. "Less time" is the median (p50) reduction over
interleaved runs; both runtimes produced the same output.

| Host | Cold start | Warm |
| --- | --- | --- |
| Windows, AMD, D3D12 | **50% less** (336 ms vs 666 ms) | **3% less** (177 ms vs 183 ms) |
| Windows, AMD, D3D12, Gemma 3 1B | **30% less** (802 ms vs 1145 ms) | 2% less (622 ms vs 634 ms) |
| Linux, AMD Radeon 8060S, Vulkan | 2% less (128 ms vs 131 ms) | **5% less** (99 ms vs 104 ms) |
| macOS, Apple M3, Metal | 3% less (125 ms vs 129 ms) | **6% less** (105 ms vs 111 ms) |

On Windows, creating the 270M model's compute pipelines takes **73% less** time
(122 ms vs 445 ms). Doe is not faster everywhere yet: single-token decode on
Apple Metal is still about 1.5-4% slower, and native (no-JavaScript) runs are
not yet measured. Earlier results from the
Zig-based runtime are not quoted here. Method and numbers:
[`reports/benchmarks/c-runtime-20261111/summary.json`](https://github.com/clocksmith/fawn/blob/main/reports/benchmarks/c-runtime-20261111/summary.json).

## Install

```bash
npm install doe-gpu
node node_modules/doe-gpu/examples/node-first-kernel.mjs
```

Prebuilt native libraries install automatically on supported platforms. See the
[support matrix](https://github.com/clocksmith/fawn/blob/main/docs/doe-support-matrix.md)
for exact operating system, architecture and backend coverage.

## Quick start

```js
import { gpu, requestAdapter } from "doe-gpu/native";

const adapter = await requestAdapter();
const device = await adapter.requestDevice();
try {
  const output = await gpu.bind(device).compute({
    code: `@group(0) @binding(0) var<storage, read> input: array<f32>;
           @group(0) @binding(1) var<storage, read_write> output: array<f32>;
           @compute @workgroup_size(4)
           fn main(@builtin(global_invocation_id) id: vec3u) {
             if (id.x < 4u) { output[id.x] = input[id.x] * 2.0; }
           }`,
    inputs: [new Float32Array([1, 2, 3, 4])],
    output: { type: Float32Array, size: 16 },
    workgroups: 1,
  });
  console.log(output); // Float32Array [2, 4, 6, 8]
} finally {
  device.destroy();
}
```

Standard WebGPU code (adapters, devices, pipelines and command encoders) works
through the same entry points.

## Entry points

| Import | Use it for |
| --- | --- |
| `doe-gpu` | The default runtime for the current host |
| `doe-gpu/native` | Doe's native provider, explicitly |
| `doe-gpu/node-webgpu-loader` | Running unchanged `webgpu`-package code on Doe |
| `doe-gpu/compute` | The compute helper |
| `doe-gpu/capture` | Recording WebGPU calls without executing them |
| `doe-gpu/browser` | Browser use; forwards to the browser's existing WebGPU, not Doe |
| `doe-gpu/browser-compiler` | Doe's WGSL compiler in a browser Worker (experimental) |

The complete list, the governed-execution and receipt APIs, the `doe-proof-node`
CLI, and build-from-source instructions are in
[REFERENCE.md](https://github.com/clocksmith/fawn/blob/main/packages/doe-gpu/REFERENCE.md).

## Choosing a native library

`DOE_WEBGPU_LIB` points at a specific Doe library. If it names a missing file,
nothing loads; there is no fallback.

## License

Apache-2.0.
