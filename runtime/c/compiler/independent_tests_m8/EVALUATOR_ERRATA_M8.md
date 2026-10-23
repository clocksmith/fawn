# Narrow evaluator errata, after candidate1 first receipts

Original case files, inventories, expected JSON and first results remain immutable.
No malformed/type-invalid case is promoted to acceptance. No candidate developer
test was inspected. This document distinguishes evaluator mistakes from defects.

1. Two originally positive finite-boundary cases are actually invalid. WGSL
2026-10-21 §3.5.2 converts literal mathematical values via §15.7.6; that algorithm
rejects a const value outside the destination's exact finite range, irrespective
of whether nearest-even IEEE rounding alone would round it back to max finite.
The decimal 3.4028234663852886e38f exceeds f32 maximum by188295816515483074560;
0x1.fffffe8p127 exceeds it by5070602400912917605986812821504. Cases
literal_concrete_float_5 and finite_just_below_overflow now require semantic
rejection with unchanged source. Exact f32 maximum and below/normal/subnormal
positive cases remain in the sealed matrix. Normative source:
https://www.w3.org/TR/2026/CRD-WGSL-20261021/#floating-point-conversion
First receipts: candidate1-semantic/scalar/results.json.

2. constructor_two_arguments remains rejected. The contract admits only zero/one
argument scalar grammar and does not require a generic call AST before rejection;
syntax2 or semantic4 is permitted. The original status4-only constraint invented
an additional implementation requirement. Six exact out-of-envelope attributes
remain rejected: group4, binding32, zero/257 workgroup dimensions and product>256.
Historical API treats unsupported resource/workgroup envelope as status3; no M8
contract requires converting that historical class to semantic4. The exact named
cases permit3 or4, not success/input/emission errors. Negative-type/constant-value
checks retain their existing class. First receipts:
candidate1-remaining/constants/results.json. Historical files are unedited.

3. legacy_exempt_plus_two_exhausts has simultaneous C=E+1 module-record and E
node exhaustion. Either correctly named budget may reject first; requiring only
'expression' was unjustified. It must still return INPUT_ERROR atomically.

4. The sealed315-node/15-statement robust logical stress is not a proven385-IR
exhaustion witness: immutable runtime let reads are reused and constants folded.
The source is valid under job caps, independently executed; fixed adapters reach
the separate4096-word ceiling and must reject atomically with OUTPUT_TOO_SMALL5
and the named word budget. Preserve this source as a word-pressure case. A new
postinspection case uses mutable bool reads in balanced logical trees, with319
nodes/16statements and >385 emitted semantic operations, to exercise actual fixed
IR exhaustion independently. This additional case is not presented as preinspection.
First capacity receipts: candidate1-later/capacity/results.json.

5. Initial signed-divide-to-unsigned mutation changed only OpSDiv's opcode, so
Vulkan1.1 correctly rejected its signed result type. This is retained as invalid
mutation construction, not oracle detection. Corrected mutation bitcasts both
operands to u32, performs OpUDiv, then bitcasts the result back to i32: target
validation passes and actual execution detects the wrong signed result. Original
15 other valid controls are unchanged. A17th valid emitted Fma mutation contracts
the sealed no-contraction fixture and is detected by exact single-rounding oracle;
that instruction was added solely to model the mutant, not to broaden source grammar.
