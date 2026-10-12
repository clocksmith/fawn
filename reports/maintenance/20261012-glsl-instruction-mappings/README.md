# GLSL instruction correctness before generation performance

Starting source: `7556862002ff55cbbc51416878a3c6a692ad3f3c`, initially clean;
Zig 0.15.2. Component: WGSL frontend, typed IR and SPIR-V emission. Intent is
preserved; no compiler/runtime ownership boundary changes.

The old emitter produced `Reflect` for runtime `ldexp`; the retained
`broken-validation.log.gz` and `broken-mappings.spv.gz` reproduce SPIR-V rejection.
The corrected emitter uses `Ldexp=53`, `Degrees=12`, `Radians=11`, and floating
`FClamp=43` for saturate, with correctly shaped F16/F32 constants.
Every handwritten GLSL extended-instruction selector in this emitter was audited
and replaced with a name generated from the pinned Khronos header. This is a
bounded encoding audit, not complete WGSL conformance.

Frontend checks reject invalid exponent types, widths and arities. Typed IR
materializes abstract integer values as floating operands. The shared runtime
fixture covers scalar and vector values, signed exponents, zero, positive and
negative exponents, and side-effecting arguments evaluated once. Its physical
driver compares explicit exact cases, including `ldexp(1.5, 3) = 12`.

## Executed checks

`final-build.log.gz` retains 5,493 passing tests and 20 skips across WGSL, core
and full-runtime suites, plus the corrected emitter and native-library builds.
The runtime fixture passes `spirv-val --target-env vulkan1.1`; separate F16 and
literal fixtures also passed. Generator drift, source ownership, schema and
CATSCAN checks passed. Binary/source identities are in `SHA256SUMS`.

The first suite failure remains in `corrected-build.log.gz`: a pre-existing test
expected SPIR-V shader-source sType 4, while the pinned upstream header specifies
1. Only that test expectation changed. Two TSIR ownership hashes were refreshed
after reviewing the comment-only changes in `2651f8670`; no TSIR code changed.

## Physical acceptance remains open

No AMD readback or application timing was run. The preflight observed Chrome
PID 3391910 using the shared GPU, and no reserved window has been confirmed.
No other process was stopped. Local raw custody is
`bench/out/doppler-generation/20261012-vulkan-campaign/`, including failed builds,
preflight snapshots and the corrected build. Existing evaluation binaries and
historical candidate verdicts are unchanged. This repair carries no speed claim.

After reservation, execute the shared fixture through the corrected library:

```sh
node bench/executors/run-glsl-instruction-controls.mjs \
  "$PROVIDER_MODULE" \
  bench/out/doppler-generation/20261012-vulkan-campaign/corrected-build/lib/libwebgpu_doe.so \
  "$NEW_READBACK_DIRECTORY"
```

`PROVIDER_MODULE` identifies the authenticated Doe Node provider. The driver
requires an AMD adapter and verifies that the declared native library was loaded.
Its report follows `config/glsl-instruction-controls.schema.json`. Retain the
complete result before admitting corrected bytes to application measurement.

Rebuild and reproduce compiler/runtime checks from `runtime/zig`:

```sh
zig build test-wgsl test-core test-full emit-spirv dropin \
  -Doptimize=ReleaseFast --prefix "$NEW_BUILD_DIRECTORY" --summary all
python3 tools/generate_glsl_std450.py --check
```
