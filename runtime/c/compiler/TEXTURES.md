# Resource-aware C compiler increment

ABI6 is an additive developer checkpoint, not physical execution or complete WGSL
qualification. Production selection stays unchanged. The public owner is
[doe_compiler_resources.h](include/doe_compiler_resources.h); ABI1–5 headers,
record layouts and original milestone seals remain unchanged. There is one
parser, semantic pipeline, resource identity domain and SPIR-V emitter.

## Source and target scope

The shared type owner represents sampled 1D/2D/2D-array/3D/cube/cube-array textures,
multisampled 2D textures, corresponding depth textures, storage 1D/2D/2D-array/3D
textures, samplers and comparison samplers. The sixteen explicit storage formats
in the ABI header support read, write and read_write declarations. Handles have
no host-shareable layout, cannot be fabricated with constructors, and cannot be
stored in structs, arrays, workgroup variables or function variables. Typed helper
parameters can receive handles. Handle-valued returns and texture_and_sampler_let
remain unsupported, not inferred from ordinary numeric constructibility.

The common typed operation owner implements textureLoad, textureStore,
textureDimensions, textureNumLevels, textureNumLayers, textureNumSamples, and
non-depth textureSampleLevel without optional offsets. Integer coordinates,
array indices and mip/sample indices accept independently selected i32/u32;
abstract integers materialize to i32. Explicit-level sampling uses f32 coordinates
and LOD, an ordinary sampler and an integer array layer. Depth loads return scalar
f32; other loads return vec4 of the declared sample type. Unsupported overloads,
formats, external textures and implicit-derivative/compare/gather operations are
rejected rather than recognized from a particular shader's text.

All operands evaluate once in source order. Handles use resolved typed resource
loads and ordinary helper formal values. Source-static resource closure retains
references in unused helpers, dead code and constant-skipped expressions under
the existing module rules. Ordinary name lookup precedes builtin classification.
Storage texture read_write loads remain potentially nonuniform; readonly loads
retain the dated dependency rules, not a fabricated workgroup-uniform result.

SPIR-V 1.3 emission uses real OpTypeImage/OpTypeSampler, UniformConstant resource
variables, OpImageFetch/Read/Write, image queries and OpImageSampleExplicitLod.
Distinct semantic access/comparison identities share identical target declarations
where SPIR-V requires canonical non-aggregate types. No host shader replay occurs.

Integer texel accesses check each signed lower bound and independent coordinate,
layer, sample and mip upper bound. Invalid loads return zero and stores discard;
image access occurs only in the admitted branch. Mip levels are checked against
OpImageQueryLevels and sanitized before OpImageQuerySizeLod. Query results for an
invalid mip are zero. Sampling coordinates retain sampler addressing semantics;
only the separate array layer is clamped before constructing sample coordinates.
No image-pointer operation, undefined query or invalid fetch is hidden behind an
after-the-fact select.

## Public metadata and physical requirements

ABI6 reuses module capacities and selection. A separate sparse HandleV6 span has
one record per texture/sampler, indexed into the unchanged module resource table.
Handle resources have NONE type/runtime-array indices and zero buffer byte-layout
fields; they do not pretend to be host data graph nodes. Buffer metadata retains
its prior exact meaning. Resource-aware reflection reports schema6. ABI1–5
reflection rejects truly unrepresentable handles, while binary-only paths do not
inherit that projection limit. All span sizing/preflight/failure publication and
workspace reuse rules are shared. A zero-capacity selected handle span is valid
but insufficient when the exact count is nonzero.

Runtime admission must check actual native view dimension, sample count, access,
format storage/sample/filter properties and enabled device features. Emitted
capabilities are Shader1, ImageQuery50 where needed, Sampled1D43/Image1D44 for 1D,
SampledCubeArray45 for cube arrays, and StorageImageExtendedFormats49 for rg32
storage formats. Explicit formats avoid read/write-without-format capabilities.
Compiler validation does not enable any physical device feature. Other targets
must implement the same typed operations or explicitly reject them.

## Bounded ownership and developer checks

No persistent record, public logical pool or queried workspace byte size grows.
Image descriptors occupy the existing TypeRecord tagged state, and calls use the
existing checked argument, IR and operand pools. Target descriptors, words and
IDs remain checked on every append; image operations fit the retained 64-ID/IR
allowance (the longest guarded texel expansion uses fewer than forty IDs).
There is no dense entry-by-resource graph or private allocation.

The new recursive image-call frame initially exceeded the inherited semantic
family subtotal. Nonrecursive numeric/swizzle finalizers now own their constant
lane temporaries outside the recursive SCC; the image argument walker reuses its
explicit result value, retaining only type/result identities between arguments.
Normal/PIC complete GCC .su/.ci audits include all seven semantic SCC members at
1520/1536 bytes, preserving the ceiling. They also charge every finalizer and
wrapper in complete acyclic paths, not merely the SCC. The fixed workspace remains
146048 bytes. These are source-bound developer bounds, not sanitizer stack peaks.

`make test-resources` runs public C/C++ ABI checks and texture-family target checks.
The target tests cover all sixteen formats, dimensions, depth/MS cases, access,
shadowing, whole-source validation, guard ordering, capability declarations,
canonical target types, source-order calls and the unchanged retained image shader.
The semantic anchor is [WGSL CRD 2026-10-21](https://www.w3.org/TR/2026/CRD-WGSL-20261021/),
sections6.6,15.2,17.7; target rules are the version1.3 instructions and capability
requirements in the [SPIR-V specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html).
Independent/integrated qualification and image instruction execution are separate.
