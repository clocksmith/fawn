# Experimental C HLSL target

Status: authored and strict-GCC compiled; developer source-emission checks and
executed scalar/aggregate/image-guard/stage-mapping C++ controls pass. **Not DXC validated, DXIL validated,
Windows/D3D12 consumed, hardware qualified, or the complete compiler migration.**
No DXC download, toolchain installation, production default, public ABI, native
resource owner or existing independent acceptance package changed.

## Boundary and implementation

`doe_hlsl_emit` borrows a fully validated shared C module, without allocation,
semantic mutation or retained pointers. The same parser, typed constants,
layout/closure analysis and instruction IDs feed existing SPIR-V and private MSL.
A measurement pass completes before byte publication. Output is a length-delimited
span; a NULL/zero span measures, and insufficient output leaves bytes untouched.
Result records expose required/written bytes, entry count, dispatch metadata and
actual emitted subgroup usage. Source/IR/result/output must not overlap or mutate
while the call runs. Error status/offset remain explicit; errno is preserved.

Implemented over current C IR:

- Concrete bool/i32/u32/f32 scalars, vectors, nested fixed arrays, structures,
  constants, immutable argument values, helper calls and multiple compute/vertex/fragment entries.
- ABI7 graphics interface leaves map to explicit HLSL semantics. Flat source
  structures remain typed values internally; the wrapper composes input formals
  and extracts returned output leaves. Position, vertex/instance index, front face,
  fragment depth, sample index/mask and location IO have explicit maps.
  Interpolation is mapped to linear/noperspective/nointerpolation and centroid/sample
  modifiers. Invariant output rejects until a DXC compilation policy establishes
  the required cross-program guarantee; adding `precise` is not treated as proof.
- Source-order instruction snapshots, explicit short circuits and structured
  continuing/break/continue, signed modulo arithmetic, safe division/remainder,
  masked shifts and sanitized float-to-integer conversions. Floating result
  temporaries use `precise`; this does not promise physical NaN/subnormal behavior.
- Explicit numeric builtin maps, including vector numerics and eleven subgroup
  arithmetic/scan operations lowered to real Shader Model 6.0 wave operations.
  Inclusive scans combine the exclusive result with the already evaluated input;
  integer signed sum/product operations use unsigned bit-preserving arithmetic.
- ABI6 sampled/depth/multisampled textures, storage textures and samplers use
  typed HLSL resource objects. Seven image operations cover guarded texel loads,
  stores, dimensions/mip/layer/sample counts and explicit-LOD sampling. Integer
  coordinates, layers, LODs and sample indices are independently guarded before
  native access. LOD is range-checked before its dimensions query; array sampling
  clamps the layer. Handle formals retain declared resource identity, not source
  text. Storage R/RG loads expand missing components to zero/one and stores select
  only actual format channels. No sampler/texture local alias or handle return is
  accepted by the current shared core.
- Resource buffers use StructuredBuffer<uint> / RWStructuredBuffer<uint> views
  with **four-byte descriptor stride**, read-only t registers and writable u
  registers. Group maps directly to register space; binding maps to register.
  Dynamic dimensions come from that bound view, not the underlying allocation.
- Uniforms use uint4 arrays in b registers. Shared WGSL byte offsets, explicit
  align/size padding, vec3's three words and nested strides determine fieldwise
  loads/stores. HLSL aggregate layout does not determine external buffer layout.
- Independent signed/fixed/runtime index guards, zero reads, discarded writes,
  checked view extents and no resource padding writes. Layout addressing is
  bounded to 32-bit bytes; invalid descriptor stride/overflow dimensions zero the
  view. Uniform buffers are bounded to the 64 KiB target limit.
- Compute invocation inputs and the retained num_workgroups metadata contract;
  groupshared roots initialize once on lane zero, then synchronize. User
  workgroup barriers emit GroupMemoryBarrierWithGroupSync, not a device fence.

A selected entry emits only its static resource/workgroup closure and reachable
function bodies. Original source entry names are retained. Names in the private
generated namespace (doe_*, Doe*, rN, wN and rN_data) reject explicitly rather than
emitting colliding declarations. Passing NO_ID emits an isolated bundle: the consuming compiler must define DOE_HLSL_ENTRY to exactly
one valid entry index. This avoids capturing another entry's resource or rejecting
legal reused bindings across entries. It is not a single simultaneously compiled
multi-entry module. Reflection remains shared-core-owned.

Emission is limited to 1 MiB, nesting to 96 and graph traversal work to
4096 * (types + members + instructions + 1) visits per entry. These are explicit
private target format/work bounds, not expanded source capacities. No VLA or heap.

## Retained path and real consumer obligations

Implementation traced these retained owners:

- `runtime/zig/src/compiler/wgsl/emit/hlsl/emit_hlsl.zig`, `_maps.zig`,
  `_builtins.zig`, `_stage.zig` and `_texture.zig` own retained HLSL translation.
- `runtime/zig/src/compiler/wgsl/pipeline/translate_hlsl.zig` analyzes WGSL,
  applies overrides, emits HLSL and attributes target diagnostics.
- `runtime/zig/src/compiler/wgsl/emit/dxil/` and
  `runtime/zig/src/compiler/wgsl/pipeline/translate_dxil.zig` are the separate
  retained direct-DXIL path. This target emits HLSL text, not DXIL containers.
- `runtime/zig/src/native/compute/doe_compute_d3d12.zig` owns the actual bounded
  native pipeline/dispatch consumer. Its existing admission is dense group0
  scalar storage arrays, one compute entry and one-dimensional dispatch. It
  creates stride-four SRV/UAV views, retains native resources and validates ranges.
  Broader HLSL authoring here does not broaden that native admission.
- `runtime/zig/src/backend/d3d12/d3d12_runtime_compute.zig` uses cs_6_0;
  `runtime/zig/src/contracts/shader_abi/dispatch_info.zig` owns num_workgroups at
  b0, space7: uint3 plus uint padding, in a 256-byte-aligned native CBV allocation.
  A conflicting uniform binding rejects. The native public compute path does not
  thereby gain uniform or dispatch-metadata support; integration remains separate.

Consumers must compile the chosen entry, honor its reflected resource layout,
create the declared word views/CBVs, bind dispatch metadata when reported, and
validate workgroup shape. Wave support requires actual D3D12 device capability.
Texture views must match the ABI6 dimension, sample scalar, format, access and
sample count. Storage textures use UAV registers, including read-only source
textures. Typed UAV load support is a real runtime format/device obligation, not
established by emission: R32 formats and additional/per-format support differ.
This target emits DXIL-bound HLSL, not SPIR-V image-format annotations. Native
query signatures and capability boundaries follow Microsoft's
[GetDimensions reference](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-getdimensions)
and [typed UAV load requirements](https://learn.microsoft.com/en-us/windows/win32/direct3d11/typed-unordered-access-view-loads).

Stage callers must select the matching vertex/pixel profile and supply matching
input layouts and linked IO. HLSL uses the original WGSL position values; native
viewport/raster conventions belong to that backend, not to this serializer.
Semantic names and modifiers follow Microsoft
[HLSL semantics](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics)
and [structure interpolation](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-struct).

## Unfinished surface

Derivative/implicit-LOD texture sampling, gather,
comparison sampling, texture offsets, external textures, matrices, f16, atomics,
pointer parameters, overrides and remaining retained builtin families are not
present in this C IR owner and remain explicitly rejected by shared admission.
Subgroup size and invocation inputs use WaveGetLaneCount/WaveGetLaneIndex;
num_subgroups and subgroup_id explicitly reject because this owner does not
assume a portable mapping from local invocation order to HLSL wave membership.
Unknown/obsolete IR operations and unknown numeric builtin identities fail before
output. Authored compute/graphics lowering
is not retained HLSL/DXIL parity or completion of the larger C migration.

The next retained compute texture gaps are `textureSampleCompareLevel` and
`textureSampleGrad`: `emit_hlsl_texture.zig` maps them to `SampleCmpLevelZero` and
`SampleGrad`. Existing witnesses are `runtime/zig/tests/core/hlsl_subsystem_test.zig`
(the compare-level case) and `runtime/zig/tests/wgsl/emit_spirv_builtin_test.zig`
(the compare-level and explicit-gradient cases). They require new shared typed
image operations and semantic admission before target lowering. Gather likewise
needs exact component semantics: the retained HLSL helper currently emits
`GatherRed` without selecting the source component; that behavior must not be
copied as a generic mapping. No target-only builtin spellings are accepted.

## Focused checks

From this directory: `make check host-control library`.

Outputs default to `/tmp/doe-hlsl-target`; override OUT as needed. `check` compiles
against the live shared C core and exercises real WGSL sources, per-entry reuse,
explicit layouts, vectors, waves, workgroup initialization, control, output sizing
and unsupported-identity mutation controls. `library` produces a private archive
whose shared compiler helper dependencies remain core-owned.

`host-control` mechanically removes stage/resource annotations from five generated
scalar/aggregate/image-guard/stage fixtures, binds word arrays, compiles their emitted
C++ operations with GCC/UBSan and checks continuing, effects, call argument order,
nested independent OOB reads, discarded-store RHS effects, integer wrapping,
bounds, view sizes, field offsets and padding preservation. The image control uses asserting
query/load stubs with sentinel data solely to execute the emitted negative/upper
LOD and coordinate guards; it does not emulate image formats, sampling or a GPU.
The stage control executes typed formal/field identity, helper-call and return-leaf
mapping after removing semantic annotations; it does not prove rasterization or
interpolation. It neither interprets WGSL nor models waves, thread scheduling, HLSL address spaces, a GPU driver or descriptor
lifetime. It is a developer control, not an independent acceptance oracle and not
proof that a target compiler accepts the source.

No historical evidence or tests were removed. The private Metal adapter's
measurement/control patterns informed this target; HLSL resource and entry
contracts remain separate rather than creating a universal emitter framework.

`arrayLength` uses the shared resolved storage-tail offset/stride and the bound
view's size metadata. It does not inspect element contents or infer a short view
from a separate allocation size. The emitted-operation fixture covers a prefixed
vector-element runtime tail, short views, partial elements and size-table identity.
