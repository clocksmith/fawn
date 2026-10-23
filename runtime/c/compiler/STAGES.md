# Vertex and fragment interfaces, ABI7

This increment admits general scalar/vector and flat-structure graphics IO in
one shared parser, type checker, typed IR and SPIR-V1.3 emitter. It is a focused
software checkpoint, not integrated graphics or physical GPU qualification.

## Public and semantic boundary

[ABI7](include/doe_compiler_stages.h) adds exactly two operations and explicit
attribute/interface capacities. It retains ABI6 resource metadata and ABI5 entry
layouts, with vertex/fragment stage values2/3 and zero graphics workgroup fields.
Sparse interface records identify each entry, direction, explicit location or
builtin, concrete scalar/width, interpolation, sampling, invariant and copied name.
All output selections perform complete validation and private target emission.
Failure publishes no words or metadata; exact counts, short/zero spans, reuse,
record-size preflight, checked products and caller errno/FP state are exercised.

Supported builtins are position, vertex_index, instance_index, front_facing,
frag_depth, sample_index and sample_mask. Location expressions use the existing
constant/type/namespace owner. Every entry rejects duplicate locations/builtins,
wrong type/stage/direction, nested IO structures and missing leaf attributes.
Integer vertex outputs and fragment inputs require explicit flat interpolation.
Vertex return paths must supply position. Dead/skipped helper references still
reject workgroup storage/barriers or writable vertex resources; subgroup use in
vertex call graphs rejects. Interstage pairing and device limits remain pipeline
creation obligations, not inferred from a standalone module.

Graphics formals retain their full semantic scalar/vector/struct values. Target
entry loading composes each formal once; IR_STAGE_INPUT identifies that formal,
and ordinary IR_RETURN retains the whole result. Shared helper functions keep
ordinary call signatures. Target interfaces alone are flattened. SampleMask uses
an explicit array<u32,1> target representation, fragment depth declares
DepthReplacing, and SampleID/sample interpolation declare SampleRateShading.
Unsupported device capabilities must reject at native admission. No feature is
enabled by compiler validation. Extension builtins, discard, derivatives, implicit
sampling and matrix/f16 families remain separate missing functionality.

## Storage and target accounting

CompilerWorkspace, every retained persistent record, fixed workspace146048 and
ABI1–6 queried layouts are unchanged. ABI7 appends a Source-owned typed workspace:
40 bytes plus32 per decorated owner,56 per flattened interface and16 per module
entry, on the qualified x86-64 ABI. Layout products/additions are checked before
binding; no dead pool is borrowed and no library allocation is introduced.

A graphics return may store more than64 leaves, so its new target-ID allowance
charges up to2 IDs per interface per IR instruction, plus4 per interface for
loading/variables. This explicit ABI7 allowance is added with uint64 arithmetic
and saturates at the target's existing uint32 format limit. Existing compute
allowances and depth/stack ceilings are not raised. An81-leaf fixture exercises
actual expanded words and IDs rather than only querying a planning budget.

Complete normal/PIC GCC .su/.ci audits include245/241 frames. Semantic SCCs are
1520/1536 bytes, within the unchanged1536 ceiling; maximum acyclic paths are
2528/2512 bytes. Public C/C++, ASan/UBSan (leaks disabled), target validation and
source regressions are distinct checks. The original CLI regression still expects
four binary-only allocations; current CLI has three (workspace, words, bytes).
Its retained failure is not hidden or edited. Eleven other CLI controls pass.

Binary-only CLI now uses explicit ABI7 capacities; legacy reflection CLI modes
remain their existing projections. Both unchanged retained vertex witnesses
(draw_call_proxy_vertex and trivial_passthrough_vertex) validate as actual SPIR-V.
The larger retained particles binary is backend provenance, not evidence that its
unavailable original WGSL or matrix-bearing whole module compiles here.

## Coordinate and integration boundary

Typed positions and emitted vertex Position preserve source-semantic components.
The current native Vulkan backend accepts raw Vulkan positive-height viewports.
WebGPU NDC-to-framebuffer Y mapping therefore still needs an explicit native
viewport/raster admission step, with an asymmetric top/bottom execution witness.
No silent compiler flip or backend behavior change is part of this checkpoint.
Full-screen validation does not prove orientation, front-face or raster parity.
The caller must also preserve the first-provoking-vertex requirement for flat
sampling `first`; `either` permits either endpoint. Native sample-rate capability
admission and actual cross-stage pipeline execution remain separate obligations.

Semantic references: [WGSL CRD2026-10-21](https://www.w3.org/TR/2026/CRD-WGSL-20261021/),
sections11.4,12,13.3; [SPIR-V specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html);
[WebGPU coordinates](https://gpuweb.github.io/gpuweb/#coordinate-systems), and
[Vulkan viewport mapping](https://docs.vulkan.org/refpages/latest/refpages/source/VkViewport.html).
