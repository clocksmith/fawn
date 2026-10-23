# Experimental C Metal target

Status: authored, GCC-compiled and developer-exercised. Not independently
accepted, Metal SDK validated, hardware-qualified or production-selected.

The private `doe_metal_emit` adapter consumes the existing validated
`Source`/`CompilerWorkspace`, without mutating it, allocating, retaining pointers,
parsing WGSL again or invoking/linking Zig. A NULL output measures exact bytes;
a second deterministic pass publishes only after complete semantic/size preflight.
Output is a byte span, not a NUL-terminated string. The caller must keep input
and IR immutable, disjoint from output and valid until return. Restore the shared
numeric environment before publishing source-compile output. This emitter uses
integer formatting and bit-pattern floating constants, not host float evaluation.

## Implemented owner

- Typed scalar/vector/array/struct values and constant DAGs; ordinary helpers and
  forward calls, source-order SSA snapshots, local mutation and returns.
- Integer wrapping arithmetic, guarded integer division/remainder, masked shifts,
  saturating float conversion, bitcasts, comparisons, scalar/vector math builtins,
  ordered min/max/clamp selection and integer dot lowering.
- Structured if, short-circuit evaluation, for/while/loop, continue through the
  continuing block, break and break-if. Typed IDs avoid name/shadowing collisions.
- Storage/uniform resources with original typed member identity and shared
  byte offsets, explicit align/size padding, vector-three packed transfers and
  nested array strides. Aggregate copy helpers read/write members, not padding.
- Independent signed/fixed/runtime index guards before any pointer formation;
  out-of-bounds loads return zero and stores discard. Runtime sizes are bytes.
- Multiple compute entry closures; source entry names are exposed with Metal
  host_name attributes. Selection filters kernel wrappers, not module validation.
  All five compute input builtins are mapped; helpers receive a thread context.
- Texture/sampler handles remain typed by shared ABI6 type/resource identity.
  All seven admitted image operations lower to MSL methods: load, store,
  dimensions, mip count, layer count, sample count and explicit-LOD sampling.
  The shared sampled/depth/multisampled dimensions and sixteen storage formats
  are covered. MSL supplies storage format conversion and absent-channel defaults;
  this target does not reimplement those conversions.
- Independent signed/unsigned texel coordinate, array-layer, mip and sample
  guards precede native reads/writes and selected-mip dimension queries. Invalid
  integer loads/queries return all-zero values and stores discard. Results reset
  on every evaluation, including loop iterations. Sampling preserves floating
  coordinates and LOD; only the separate integer array layer is clamped.
- Texture/sampler formals and typed global-handle closures are ordinary MSL
  parameters. Writable textures never become local values or nullable context
  fields. Empty entries and resource aliases used by separate entries remain
  independent; names or source spelling do not select resource identity.
- Threadgroup roots initialize on local lane zero, then synchronize before user
  code. User barriers fence threadgroup memory only. Aggregate threadgroup copies
  are fieldwise to preserve Metal address-space ownership.

## Retained path traced

Semantic/reference owners read during implementation:

- `runtime/zig/src/compiler/wgsl/emit/msl/emit_msl.zig` delegates to
  `emit_msl_ir.zig`, with stage/builtin/texture/subgroup/workgroup helpers.
- `runtime/zig/src/compiler/targets/msl.zig` defines the MSL 3.0 baseline:
  1024 workgroup invocations and 16 KiB threadgroup storage floor.
- `runtime/zig/src/compiler/wgsl/pipeline/translate_msl.zig` and the native
  compiler path supply shader source/entry data to Metal consumers.
- `runtime/zig/src/native/queue/doe_queue_submit_metal.zig` and
  `runtime/zig/src/native/compute/doe_compute_fast.zig` populate byte lengths in
  slot 30. Buffer slots remain group * 16 + binding, with binding <16 and
  slots 0..29; slot 30 remains reserved for byte sizes.

The actual runtime must bind slot 30 whenever `needs_sizes_buffer` is true.
Texture and sampler arguments use their separate native argument tables, with
slots also group * 16 + binding and binding <16. This target conservatively admits
texture slots 0..30 and sampler slots 0..15. Texture slot 30 does not collide with
buffer slot 30. It rejects out-of-range slots rather than silently compacting or
inventing a runtime remapping. The host must bind each actual reflected view and
sampler at those exact native indices; this source owner does not change runtime
binding or negotiate device limits. Per-entry checks permit aliases across
separate entries but reject duplicate binding identities within one entry.

Native MSL 1D textures have one base level and no explicit-LOD sample overload.
For WebGPU 1D views, the emitted mip count is 1, integer LODs are independently
checked against 1 before width/read operations, and sampling uses the sole level.
The source LOD expression still evaluates once in order. A host-supplied image
outside WebGPU's 1D single-level invariant is not supported by this contract.

Workgroup dimensions remain in the shared validated entry records and reflection;
the runtime must dispatch that shape. No native queue/resource behavior changed.

## Explicit unfinished surface

Vertex/fragment wrappers use the shared typed stage view and have focused emitted-
operation controls, not SDK validation. f16, atomic operations and pointer-
parameter target owners remain unfinished. Shared subgroup admission
exists, but this Metal owner still explicitly rejects all eleven collective
arithmetic operations and four used subgroup inputs. Other missing features
reject at shared admission or the explicit target boundary, never with fake
output. This source progress does not establish retained Metal owner completeness.

Emission is bounded at 1 MiB, nesting at 96, and type traversal at
4096 * (types + members + instructions + functions + 1) visits. Global handle call-closure traversal has a
separate budget of 4096 * (instructions + functions + resources + 1) visits. These are explicit target work/format limits,
not extensions to shared source capacities. No library heap or proportional VLA.

## Quick developer checks

From this directory:

    make check host-control image-host-control library

`make library` creates a reusable private target archive; its shared compiler
helper dependencies remain owned by the existing C core.

All build/generated outputs go to `/tmp/doe-metal-target` by default; override
OUT when needed. The first target links ordinary GCC C against the same shared
parser/sema and checks real residual-style sources, explicit layout, vectors/math,
workgroups, multiple entries, output sizing, deterministic publication, and
wrong-but-typed rejection. The host controls compile the emitted scalar/aggregate/image
C++ operations with GCC and execute continuing/effects/wrapping/bounds/padding
checks. They deliberately do not emulate Metal address spaces, builtins, threads
or driver compilation, and are not a second WGSL interpreter or acceptance oracle.
Image controls use method spies with explicit sentinels, strict integer argument
checks and recorded sampling operands. They test emitted sequencing/bounds and
method mapping, not pixels, filtering, texture memory or native format conversion.

The image mapping was checked against Apple's official [MSL specification](https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf),
version 4.1 dated 2026-06-04, sections 2.9, 2.10, 5.2.1 and 6.13.1..6.13.14;
only the existing MSL 3.0 baseline methods are used. The [WebGPU 1D texture contract](https://www.w3.org/TR/webgpu/#enumdef-gputexturedimension)
requires the single-level mapping above. [Image author evidence](evidence/image-implementation.md)
records source hashes, commands, first failures and scope.

Existing independent tests and historical accepted receipts remain untouched.
The shared compiler build/API is not switched to this owner. Parent integration
owns any shared dispatch or public ABI changes.

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

Graphics stage adaptation now consumes ABI7 typed vertex/fragment interfaces,
including flat input structures, location/interpolation, builtins and return
leaves. It reuses the same resource/context/function owners as compute. Run
`make graphics-host-control` for the attribute-erased operation test, not MSL
compiler acceptance. `@invariant` rejects until the native build owns and qualifies
Apple's `-fpreserve-invariance` requirement (MSL4.1 section1.6.4); an attribute alone
would be ignored. Graphics pipeline/device/layout qualification remains separate.

The private emitter now requires `DoeTargetScratch`, sized by
`doe_target_scratch_bytes(module, &bytes)`. The caller owns that exclusive byte
span, disjoint from inputs/results/output, and may reuse it after any return
without resetting. One byte per function holds the selected completed closure;
one per type (including seven primitives) prevents repeated aggregate subtrees.
No allocation, fixed function-count cap, IR mutation or larger work budget was
introduced. Each selected entry closure visits each reachable function once;
MSL's all-entry mode resolves the union once. Shared call DAGs are not unrolled.

## Matrices and continuing discard

All nine f32 matrix shapes use native column-major MSL types. Shared column
strides drive storage/uniform transfers, including packed three-row columns;
native matrix/vector/scalar products, transpose and determinant preserve the
shared operand identities rather than applying scalar-per-lane lowering. Typed
zero helpers fill only selected aggregate fields/columns in compact loops, never
padding, flattened repeated constants or uninitialized native matrix defaults.

`discard_fragment()` is emitted without a synthetic return. Generated text
enforces the existing MSL3.0 baseline: Apple’s MSL4.1 specification sections2.3,
5 and6.11.1.3 define matrix construction/operators and continuing helper behavior
from MSL2.3 onward. Ordinary HLSL remains explicitly unsupported for discard.

`make matrix-host-control` checks all nine source shapes and executes the emitted
finite exact arithmetic/access operations with column-major C++ control types,
poisoned default matrices, short buffers and padding sentinels. This is not an MSL
parser, SDK validation, GPU simulator, floating-point accuracy proof or physical
qualification. Source-level workgroup/fragment mapping and failure-atomic atomic-
operation rejection have separate focused controls.
