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
  slot 30. Resource slots are group * 16 + binding; this target rejects binding
  values outside a group and any slot >=30 instead of colliding with sizes.

The actual runtime must bind slot 30 whenever `needs_sizes_buffer` is true.
Workgroup dimensions remain in the shared validated entry records and reflection;
the runtime must dispatch that shape. No native queue/resource behavior changed.

## Explicit unfinished surface

The current C IR has no vertex/fragment stage interface, texture/sampler, matrix,
f16, atomic or pointer-parameter instructions. Their retained Zig MSL
owners are not translated yet. Shared subgroup admission now exists, but this
Metal owner explicitly rejects all eleven collective arithmetic operations and
four used subgroup inputs until their target rules are implemented. ABI6 image
handles and image operations also reject atomically rather than being mistaken
for buffer values; sampled-load and storage-write rejection controls cover this
shared-IR extension. Other missing
features reject at shared admission, never with fake target output. New/obsolete IR opcodes, malformed target
identities, unrepresentable slots, over-budget workgroups and unsupported value
types reject explicitly. This is a coherent compute-target implementation over
the current C IR, not a claim of retained Metal owner completeness.

Emission is bounded at 1 MiB, nesting at 96, and type traversal at
1024 * (types + members + 1) visits. These are explicit target work/format limits,
not extensions to shared source capacities. No library heap or proportional VLA.

## Quick developer checks

From this directory:

    make check host-control library

`make library` creates a reusable private target archive; its shared compiler
helper dependencies remain owned by the existing C core.

All build/generated outputs go to `/tmp/doe-metal-target` by default; override
OUT when needed. The first target links ordinary GCC C against the same shared
parser/sema and checks real residual-style sources, explicit layout, vectors/math,
workgroups, multiple entries, output sizing, deterministic publication, and
wrong-but-typed rejection. The host controls compile the emitted scalar/aggregate
C++ operations with GCC and execute continuing/effects/wrapping/bounds/padding
checks. They deliberately do not emulate Metal address spaces, builtins, threads
or driver compilation, and are not a second WGSL interpreter or acceptance oracle.

Existing independent tests and historical accepted receipts remain untouched.
The shared compiler build/API is not switched to this owner. Parent integration
owns any shared dispatch or public ABI changes.
