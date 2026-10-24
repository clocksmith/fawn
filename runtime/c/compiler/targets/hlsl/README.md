# Experimental C HLSL target

Status: authored and strict-compiled; developer source-emission checks and
executed scalar/aggregate/image-guard/stage-mapping C++ controls pass. Five public
ABI11 controls also pass actual DXC compilation and register/wave metadata
comparison; see [the source-bound receipt](../../DEVELOPER_RECEIPT_HLSL_ABI11.json).
This is not complete HLSL/DXIL semantic acceptance, Windows/D3D12 consumption,
hardware qualification or the complete compiler migration. DXC is an explicitly
selected external test tool; no system installation, production default, native
resource owner or independent acceptance package changed. The additive public ABI11
facade is described below; ABI1-10 layouts and behavior remain unchanged.

## Public typed consumption

[`doe_compiler_hlsl.h`](../../include/doe_compiler_hlsl.h) adds two exported
functions: `doe_compiler_hlsl_requirements` and `doe_compiler_compile_hlsl`.
They share the existing text API's capacities, flags, result, source pipeline,
measure/staging and atomic publication owner. The result target is HLSL and
language version is 600 (Shader Model 6.0). Text remains exact, unterminated bytes.

Typed binding records carry the source resource index, CBV/SRV/UAV/sampler
namespace, shader register, register space and descriptor stride. Entry records
carry the actual DXC entry name, original entry index (also the bundle selector),
per-entry wave requirement and any dispatch-CBV register/space/byte span. Unused
dispatch records use NONE/NONE/zero. These records are captured by the same target
declaration and entry emitters that produce HLSL, not reconstructed from source
or independently assigned by a frontend. Whole-module schema9 reflection and
entry-reference membership remain unchanged; target records describe the emitted
union only. Entryless NONE owns zero text and target records.

Run `make -C runtime/c/compiler test-hlsl-text` from the repository root. On
Darwin, select a dylib with `SHARED_LIBRARY=/tmp/output/libdoe_compiler.dylib`;
`TEXT_TEST_BUILD` chooses the consumer output directory. Ordinary typed C/C++
consumers exercise emitted register identity, selected/all-entry metadata, wave
and dispatch requirements, exact sizing, output-selection rules, final ownership
and failure publication. The existing private target checks remain separate and
unchanged. Neither consumer compilation nor emitted-operation controls qualify
DXC, DXIL or physical execution by themselves.

For an actual DXC check, give the public C consumer one fresh existing output
directory as its sole argument. It writes selected-entry and entry-bundle HLSL
with the actual public binding/entry records. Then run:

```sh
python3 runtime/c/compiler/tests/test_hlsl_dxc.py --dxc /path/to/dxc \
  --fixtures /path/to/public-consumer-output --output /fresh/dxc-results
```

This optional check compiles five compute entries/selections, preserving the
first failure. It compares DXC's actual register tables and wave requirements
with the public metadata, hashes inputs/outputs and refuses an existing result
directory. It never downloads tools, changes a checkout or executes GPU code.

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

- Concrete bool/i32/u32/f32 scalars, vectors, all nine f32 matrix shapes, nested fixed arrays, structures,
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
  typed HLSL resource objects. Eight image operations cover guarded texel loads,
  stores, dimensions/mip/layer/sample counts, explicit-LOD depth/color sampling
  and explicit mip-zero depth comparison. Matching ordinary/comparison sampler
  types and checked constant depth offsets retain the shared image contract. Integer
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
comparison with derivatives, offsets on remaining non-depth sampling overloads,
external textures, f16, pointer parameters,
overrides and remaining retained builtin families remain incomplete. New shared
atomic IR is lowered through the canonical guarded access owner as described below.
Subgroup size and invocation inputs use WaveGetLaneCount/WaveGetLaneIndex;
num_subgroups and subgroup_id explicitly reject because this owner does not
assume a portable mapping from local invocation order to HLSL wave membership.
Unknown/obsolete IR operations and unknown numeric builtin identities fail before
output. Authored compute/graphics lowering
is not retained HLSL/DXIL parity or completion of the larger C migration.

The next retained explicit texture gap is `textureSampleGrad`:
`emit_hlsl_texture.zig` maps it to `SampleGrad`, and
`runtime/zig/tests/wgsl/emit_spirv_builtin_test.zig` retains its witness.
Explicit depth comparison now uses the shared IMAGE_SAMPLE_COMPARE_LEVEL
operation and SampleCmpLevelZero, including the retained compare-level family.
Gather likewise
needs exact component semantics: the retained HLSL helper currently emits
`GatherRed` without selecting the source component; that behavior must not be
copied as a generic mapping. No target-only builtin spellings are accepted.

## Focused checks

From this directory: `make check host-control library`.

Outputs default to `$(DOE_C_TMP)/hlsl-target` (per-checkout root, [build-root.mk](../../../build-root.mk)); override OUT as needed. `check` compiles
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

Memory helper emission follows selected entry call reachability and actual access
root/type/mode closure. It no longer generates every concrete type for every
buffer. Vector transfers are helper leaves; arrays and structures include their
called child helpers. Metal retains whole-workgroup initialization helpers and
omits unrelated function bodies/context resources when one entry is selected.
These read-only walks have a 96-frame bound and charged work, no allocation or IR
mutation. Shared malformed-graph tests and multi-entry helper-absence tests run in
`make check`; source-level unused declarations do not become resource accesses.

The private emitter now requires `DoeTargetScratch`, sized by
`doe_target_scratch_bytes(module, &bytes)`. The caller owns that exclusive byte
span, disjoint from inputs/results/output, and may reuse it after any return
without resetting. One byte per function holds the selected completed closure;
one per type (including seven primitives) prevents repeated aggregate subtrees.
No allocation, fixed function-count cap, IR mutation or larger work budget was
introduced. Each selected entry closure visits each reachable function once;
MSL's all-entry mode resolves the union once. Shared call DAGs are not unrolled.

## Matrix value representation

HLSL native rows represent WGSL columns: WGSL matCxR uses HLSL floatCxR, whose
native dimensions are C rows and R columns. Indexing and compact column constant
DAGs stay direct. Matrix addition/subtraction and scalar multiplication are
componentwise; linear matrix/vector products use `mul(right, left)` because the
internal value is transposed. Transpose and determinant map directly. This avoids
wrapper copies or an additional semantic/layout engine. Shared WGSL offsets and
column stride still own external buffers, including vec3 padding; native HLSL
packing never determines them. The mapping follows Microsoft's
[matrix dimensions](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-matrix),
[mul operand rules](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-mul)
and [scalar-to-aggregate casts](https://learn.microsoft.com/en-us/windows/win32/direct3d9/casting-and-conversion).

The common matrix source fixture is shared with Metal, but each host operation
model uses its target's actual row/column convention. Seventeen exact finite
outputs cover nonsquare matrix/vector products, a noncommuting square product,
scalar/column constructors, transpose, determinant, zero and padding. A deliberate
same-shaped product reversal must fail. The host shim preserves HLSL aggregate
zero casts rather than incorrectly calling poisoned C++ default constructors.
This is emitted-operation evidence, not DXC, DXIL, GPU precision or SDK acceptance.


## Atomic operations

All eleven existing WGSL atomic IR operations lower for writable storage and
workgroup i32/u32 memory. The existing access-path owner checks negative and
upper indices, runtime-tail length and the four-byte extent before forming a
native lvalue. Results begin at zero (including compare-exchange's false flag)
so invalid accesses retain guarded load-zero/store-discard behavior. Source
operand and index side effects still occur once in their validated IR order.

Storage views and groupshared atomic leaves are native uint words. Add/subtract
and bitwise operations preserve modular bits; signed results use asint. Signed
min/max use a strong CAS retry loop with signed comparisons, avoiding unsigned
ordering from the raw word view. Compare-exchange maps the canonical comparator
and replacement, returning the observed old word and success equality. A strong
implementation satisfies WGSL's weak exchange contract. Atomic load uses an
InterlockedOr with zero and store uses InterlockedExchange. These interlocked
operations add no independent memory fence; existing workgroup barriers remain
separate. See Microsoft's [compare-exchange contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/interlockedcompareexchange)
for native memory and loop-attribute requirements.

The private check covers every signed/unsigned storage/shared operation, nested
runtime-tail access and malformed operand publication. The former atomic-store
rejection in the matrix control is now an explicit admission and too-small-output
control; unrelated pointer and unknown-IR rejection stay intact.

For actual software execution of the emitted target:

```sh
make atomic-software OUT=/tmp/doe-hlsl-atomic DXC=/absolute/path/to/dxc \
  SOFTWARE_VULKAN_ICD=/absolute/path/to/lvp_icd.json
```

This compiles five fresh emitted fixtures to cs_6_0 DXIL, translates their exact
HLSL bytes to SPIR-V with the same official DXC, validates them, and executes on
an explicitly selected CPU Vulkan adapter. Fixed expected words check old values,
wrap, signed min/max, successful/failed exchange, negative/high-index zero results,
canaries and ordered operands even with an empty runtime tail. DXC's SPIR-V mode
warns that allow_uav_condition is ignored; DXIL accepts the required attribute.
These controls do not establish Windows/D3D12 execution, physical execution,
concurrent forward-progress qualification, or the separate Metal atomic target.
