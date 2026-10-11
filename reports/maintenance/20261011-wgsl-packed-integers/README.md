# WGSL packed integer correctness

Doe now admits the signed and unsigned packed-byte operations, including clamp,
unpack, and dot-product variants, with typed shared IR lowering and constant
folding. The compiler accepts `requires packed_4x8_integer_dot_product;`; native
C and JavaScript interfaces expose the language feature. The independent
[Doe receipt](doe.json) passes the retained ordinary AMD/Vulkan controls.
[Manifest](manifest.json) owns source identity, test totals, artifacts, and scope.

## Repairs and ownership

`ir_packed_integer.zig` owns signedness, truncation, clamping, byte order, and
constant evaluation. Runtime lowering uses masks, shifts, bitcasts, and integer
reductions. Source-target integer-dot helpers evaluate each vector argument once
and preserve integer arithmetic rather than invoking floating-point `dot`.
Wrong types and arity reject. Abstract inputs must fit their materialized type.
Module constants include splats, mixed vector constructors, and inferred vectors.
Allocation-failure controls exercise lowering and constant ownership.

The Vulkan emitter repairs the GLSL.std.450 instructions for `pack4x8unorm`,
`pack4x8snorm`, and `pack2x16float`. The retained [baseline shader](baseline.wgsl)
and [validator failure](baseline-validation.log) show the operand mismatch.
The baseline binary came from the retained local generation investigation build,
not a fresh build of the manifest's source base. This is a bounded compiler
reproduction, not a before/after performance experiment.

Correct instruction selection exposed a separate rounding defect. WGSL specifies
`floor(0.5 + scale * clamp(value))` for normalized packing. The tested driver packs
halfway values differently through the GLSL instruction alone. Doe prequantizes
with the specified floor operation before native packing. MSL/HLSL translation
uses that rule too. Previously admitted normalized 16-bit packing and unpacking
now have explicit Vulkan lowering and HLSL helpers.

## Physical controls and independent references

The repo-only [runner](../../../bench/executors/run-packed-integer-controls.mjs)
creates ordinary shader modules, pipelines, bindings, and commands. It submits,
maps readback, waits for queue completion, and destroys buffers and the device.
It requires explicit Doe selection and observes the exact library in the process;
there is no silent provider substitution. Adapter and driver identity are retained
in [Vulkan discovery](vulkan-summary.txt) and the receipts.

The CPU oracle uses arithmetic byte extraction, sign extension, modular packing,
clamping, and scalar sums, independently of Doe's IR and generated shaders.
Controls exhaust byte-pair combinations and add boundary and seeded mixed words.
Separate controls check side-effecting arguments, module constants, normalized
packing, binary16 packing, and normalized 16-bit roundtrips.

Integer and normalized outputs require exact words. The frozen non-exact binary16
input admits only its adjacent representable results, as permitted by WGSL's
correctly-rounded conversion rule. The earlier harness incorrectly demanded
nearest-even for that input. Earlier failed runs remain in the raw archive;
correcting that oracle did not relax the normalized or integer checks.

The [Dawn control](dawn.json) passes integer, single-evaluation, and constant
checks but fails the independent normalized-packing tie cases. Its failing
outputs and thresholds are retained. Provider agreement alone is not the oracle.
The generated SPIR-V passes `spirv-val` for Vulkan; [translation receipts](shader-validation.json)
also retain MSL/HLSL emission. These are source-translation checks, not physical
Metal or D3D12 acceptance.

## Reproduction and retained scope

From `runtime/zig`, run the commands in the manifest to test and build a private
prefix. From the repository root, invoke the runner with the provider module,
exact library, and an unused output directory. A Dawn arm uses `dawn` as the
library argument. It should retain a failed verdict on the tested incumbent's
normalized tie behavior. Validate each emitted fixture with the recorded validator
command. The [raw archive](raw-controls.tar.gz) preserves shaders, frozen inputs,
expected outputs, mapped outputs, final receipts, and earlier failures. Source
changes and new module hashes bind the build without committing its shared library.

This qualifies the retained packed-operation workload on AMD/Vulkan. It does not
qualify complete WGSL/WebGPU conformance, dedicated hardware dot instructions,
physical Metal/D3D12 execution, or inference acceleration. Portable integer-dot
lowering remains active. Doppler model arithmetic and sampling are unchanged.
No package publication or deployment accompanies this change.

Normative references: [WGSL candidate recommendation](https://www.w3.org/TR/2026/CRD-WGSL-20260921/),
[Khronos GLSL.std.450 grammar](https://github.com/KhronosGroup/SPIRV-Headers/blob/main/include/spirv/unified1/extinst.glsl.std.450.grammar.json).
