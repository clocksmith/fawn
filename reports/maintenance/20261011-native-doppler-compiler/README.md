# Current Doppler compiler compatibility on Vulkan

Current Doppler initially failed to prepare its RoPE shader through Doe while
Dawn executed the frozen case. The minimized logarithm shader emitted an invalid
GLSL integer-search instruction. A returning switch then exposed incorrect merge
termination; a nested-switch control exposed interleaved IR case ranges.

The corrections belong to builtin emission, function control flow, and AST-to-IR
case publication. Shader source, model precision, context and consumer operators
remain unchanged. Selector allocation rollback is tested with allocation faults.

The [manifest](manifest.json) binds the source patch, native library, minimized
shaders, original failure, regression logs and physical Doe/Dawn readbacks.
`zig build test-wgsl` and `zig build test` pass. The corrected SPIR-V fixtures pass
`spirv-val --target-env vulkan1.1`. Installed generation, cancellation and reuse
are qualified separately by the subsequent local-memory campaign.

This is bounded compatibility evidence. It establishes neither general WebGPU
conformance nor an application performance advantage. Older rejected performance
experiments retain their original verdicts.

Reproduce the native build with `zig build dropin emit-spirv
-Doptimize=ReleaseFast --prefix <absolute-output>` from `runtime/zig`. Run
`node bench/external-projects/doppler-generation/run-compiler-controls.mjs
<provider-manifest> <doe-or-dawn> <receipt>` from the repository root.

Component: WGSL compiler. Intent: preserved. Acceptance evidence: linked
regression logs and physical readbacks. Boundary effects: current Doppler shader
compatibility; public APIs, execution ownership and backend support are unchanged.
