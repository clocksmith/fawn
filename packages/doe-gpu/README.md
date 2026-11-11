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

Selected results from Fawn's indexed benchmark reports. Each one compares
Doe with Dawn on the same workload and machine. "Less time" is the median (p50)
reduction. These were measured with the earlier Zig-based Doe runtime; the
package now ships the C runtime, and these workloads have not yet been re-run
on it.

| Workload | Host | Compared with | p50 time |
| --- | --- | --- | --- |
| Gemma 3 270M inference (64 + 64 tokens), Node | AMD, Vulkan | `webgpu` npm package (Dawn) | **13.8% less** |
| Same, Bun | AMD, Vulkan | `webgpu` npm package (Dawn) | **14.0% less** |
| Same, Node | Apple, Metal | `webgpu` npm package (Dawn) | 3.6% less |
| Stable-fluids simulation, 256² grid | AMD, Vulkan | Dawn, native | **84% less (6.4×)** |
| Monte Carlo path tracing, 131k paths | AMD, Vulkan | Dawn, native | **63% less (2.7×)** |
| Matrix-vector, 32768 × 2048 f32 | AMD, Vulkan | Dawn, native | 45% less (1.8×) |
| Buffer-to-texture copy | Apple, Metal | Dawn, native | 78% less (4.6×) |

Doe isn't faster on every workload, and these are single-machine results. Every
row comes from a claim-indexed report (with p95 and per-run samples) listed in
[`reports/claim-index.json`](https://github.com/clocksmith/fawn/blob/main/reports/claim-index.json);
rows marked diagnostic there are not quoted here.

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
