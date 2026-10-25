# Native translation metadata

ABI17 in `include/doe_compiler_translation.h` extends ABI16 specialization with
failure-atomic, per-entry translation records. Capacity queries report the exact
record count; an insufficient output publishes no records. Entry indices refer to
the returned entry-point array. Existing ABI layouts remain unchanged.

## Immediate address space

`requires immediate_address_space` and read-only `var<immediate>` declarations
use the shared type/layout and entry-reachability owners. Types must be fixed,
host-shareable and constructible; arrays, atomic types and aggregates containing
arrays are excluded. An entry may statically use at most one immediate variable.
Immediate bytes are not user bind-group resources or workgroup storage.

Each record reports the canonical byte extent and an accessible-byte mask for
bytes 0 through 63. Padding bits are zero. A consumer with a 64-byte limit must
reject larger extents before using that mask. Zero extent explicitly means no
immediate data. Target bindings are reserved by the compiler: SPIR-V PushConstant,
Metal buffer slot 29, and HLSL constant register b1 in space7. Metal transfers and
HLSL packed constant reads preserve canonical WGSL offsets rather than relying
on native structure layout. Native pipeline layouts, initialized-byte validation
and immutable command snapshots remain consumer responsibilities.

`make test-translation` checks all three target APIs, selected/absent entries,
padded-layout masks, capacity failure and caller errno preservation. Compilation
and SPIR-V validation are developer checks, not SDK or physical-device acceptance.
Pointer metadata and lowering use the shared semantics described below.

## Other target requirements

HLSL entry flags report waves, native 16-bit types, generated wave-counter shared
memory, and native array texture representations. The wave-counter flag adds the
publicly specified four-byte target overhead to semantic workgroup storage.
Native array-view representation permits a selected 2D layer or cube at a nonzero
base array layer; the WGSL source type and coordinate arity do not change.
Raw-storage requirements are described below; native-device admission remains separate.

## Native builtin developer surface

`test-native-builtins` covers normalized/half float packing, quantization,
derivative variants, subgroup vote/exchange/arithmetic and quad operations using
fresh target outputs and independent constant expectations. Stage legality and
uniformity use the shared semantic graph. Subgroups require the source extension
and are rejected when `DOE_COMPILER_DISABLE_SUBGROUPS` is set. This test is a
focused developer gate, not a hardware qualification claim.

## Raw half storage

HLSL half-containing storage bindings use ByteAddressBuffer/RWByteAddressBuffer
and ABI11 stride zero. RAW_STORAGE entries require exact logical binding sizes
at b2, space7, one uint per canonical reflected resource index, padded into uint4
carriers. The table uses the actual selected module's resource count, not source
binding numbers or a fixed native limit. Native raw descriptor padding must not
increase these logical sizes. Tables larger than the 64 KiB CBV limit fail closed.
Half loads/stores use native float16_t operations with two-byte alignment; mixed
32-bit leaves and atomic operations retain their original byte offsets. The
producer must honor NATIVE16 and compile with the corresponding DXC requirements.
See Microsoft's [templated byte-buffer contract](https://github.com/microsoft/DirectXShaderCompiler/wiki/ByteAddressBuffer-Load-Store-Additions).
`test-hlsl-raw` checks these source and metadata contracts, not Windows execution.

`test-language-aliases` checks immutable texture/sampler SSA aliases and implicit
pointer-composite reference access across all three targets. These reuse existing
resource identities, sampling-pair analysis, pointer effects and robust bounds;
generalized pointer calls are handled by the shared specialization owner below.

The HLSL target's `check-current` derives its runner from the unchanged historical
`test_hlsl.c`: two whole-root pointer negatives become positive inout/lvalue
checks, and the obsolete synthetic subgroup-count/id rejection is replaced by a
source-level generated wave-counter check. Every other historical assertion is
retained. This adapter does not imply SDK or hardware acceptance.

## Shared pointer specialization and uniformity

After shared alias and uniformity analysis, the bounded typed-IR specialization
owner widens a subobject pointer formal to the complete function-local root and
adds scalar formals for already-evaluated dynamic indices. Callee access paths
retain the full prefix and all robustness checks. Forwarded pointers and repeated
calls retain their identity. All targets consume this same IR; there is no source
rewrite or target-specific interpretation of pointer aliases. Pool exhaustion
fails before output publication. No SPIR-V VariablePointers capability is added
for Function pointers. Non-function pointers retain their canonical global root:
shared specialization removes their pointer formals rather than copying buffers
or shared variables. Dynamic indices remain scalar arguments evaluated once.
Atomic, runtime-array length and uniform-load operations use this same owner.

The existing uniformity analyzer models address and pointee sensitivities
separately. Per-output call summaries preserve constant whole-root overwrites,
partial-write dependencies, aliasing, and control/return joins. Additional bounded
scratch borrows the dead value-ID pool; insufficient capacity fails atomically
with an exact minimum IR capacity rather than allocating hidden storage.
`test-global-pointers`, `test-pointer-uniformity` and
`test-pointer-uniformity-capacity` cover the current positive/negative contracts.

Optional graphics enables are consumer-authorized via the additive typed flags
DISABLE_CLIP_DISTANCES, DISABLE_PRIMITIVE_INDEX and DISABLE_DUAL_SOURCE_BLENDING.
Module validation and selected-entry compilation share this admission owner.
`test-feature-admission` checks each denied/allowed directive and caller state;
feature advertising and actual backend support remain native responsibilities.
