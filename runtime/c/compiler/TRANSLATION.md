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
Generalized pointer parameters remain a separate language migration gate.

## Other target requirements

HLSL entry flags report waves, native 16-bit types, generated wave-counter shared
memory, and native array texture representations. The wave-counter flag adds the
publicly specified four-byte target overhead to semantic workgroup storage.
Native array-view representation permits a selected 2D layer or cube at a nonzero
base array layer; the WGSL source type and coordinate arity do not change.
The reserved raw-storage flag is not evidence of an implemented raw-buffer route.

## Native builtin developer surface

`test-native-builtins` covers normalized/half float packing, quantization,
derivative variants, subgroup vote/exchange/arithmetic and quad operations using
fresh target outputs and independent constant expectations. Stage legality and
uniformity use the shared semantic graph. Subgroups require the source extension
and are rejected when `DOE_COMPILER_DISABLE_SUBGROUPS` is set. This test is a
focused developer gate, not a hardware qualification claim.
