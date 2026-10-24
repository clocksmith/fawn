# HLSL target owner

Keep this private adapter on the shared C typed IR. Do not duplicate admission,
constant semantics, module closure ownership or the parser. `hlsl.h` owns the
borrowed-module/output contract; this directory does not select a public target.
The additive public ABI11 facade in `../../include/doe_compiler_hlsl.h` calls this
same serializer. Keep its private metadata staging attached to actual register
declarations and entry emission; do not recreate register/feature policy in the
facade or a runtime consumer. Existing ABI10 Metal records never become HLSL records.

- Resource identity is the resolved declaration index. A selected entry emits
  only its static resource/workgroup closure and reachable function bodies.
  Bundles isolate entries with DOE_HLSL_ENTRY; do not merge equal binding numbers
  from different entries or put unrelated handles in a common argument context.
- Structured buffers are uint words with exactly four-byte descriptor stride.
  Use the shared byte layouts for offsets, array strides and member transfers;
  HLSL struct or cbuffer packing is not WGSL layout. Never copy resource padding.
- Guard signed indices and each indexed subobject before forming offsets or
  accesses. Loads initialize zero; invalid stores discard. Preserve IR value
  snapshots, source-order call arguments, short circuits and continuing paths.
- Texture handles preserve typed declaration/formal identity. Query a valid base
  mip first, then guard the requested mip before its dimension query and every
  coordinate/layer/sample before a texel operation. Sampling array layers clamp.
  Storage image format channels govern load expansion/store projection. Read-only
  storage textures use SRV declarations and mip-zero Load/GetDimensions forms;
  one target predicate owns declaration, access and reflected register class.
  Write/read-write storage remains UAV; logical storage usage stays upstream. Typed UAV
  device capabilities remain runtime obligations, not inferred support.
- Stage interfaces come from shared ABI7 typed leaves, not source attribute replay.
  Preserve formal/member identity and original return values; stage semantics and
  interpolation are explicit target maps. Invariance requires a qualified target
  compilation policy, not merely a `precise` spelling.
- Subgroups use explicit Shader Model 6 wave intrinsics, not workgroup memory or
  host execution. Extend closed builtin/input mappings deliberately; unknown
  operations reject before publication. Dispatch metadata follows retained b0,
  space7, and conflicts reject rather than silently changing the native ABI.
- Run `make check host-control library`. These establish native C compilation and
  bounded developer controls, not DXC, DXIL, D3D12 or physical GPU acceptance.

Shared read-only value IDs, ordered access-path traversal and runtime-tail metadata
live in `../ir_view.h`. Keep target syntax/address policy outside that leaf;
`../tests/test_ir_view.c` exercises malformed identities independently of emitters.

Emit memory helpers only for selected reachable access roots, transfer types and
read/write modes, including recursive aggregate children. `../closure.h` owns the
read-only bounded call/dependency queries in explicit caller-owned scratch.
Resolve the function closure once and remember completed DAG nodes; a depth bound
alone does not bound repeated shared subtrees. Do not grow a second semantic graph.
Workgroup zero initialization also requires whole-root store helpers. Keep target
address spaces and byte-view validation local. All walks charge composed work.

Matrix native rows are WGSL columns, so linear products reverse operands in mul;
componentwise scalar products do not. Keep indexing, constant DAG children and
byte-layout column stride on that one representation. The shared matrix fixture
and wrong-order control protect against accidental target-convention mixing.

Atomic IR uses the same access-path bounds/byte guard as ordinary memory. Never
form the destination lvalue before every index and the four-byte extent pass.
Storage and workgroup atomic<i32/u32> use native uint words; reinterpret signed
results and implement signed min/max with a retrying signed-comparison CAS loop,
not unsigned native min/max. Compare-exchange initializes both old_value and
exchanged before guards; strong exchange is a permitted weak implementation.
Ordered typed operand IDs are snapshots, never replayed source expressions.
Interlocked operations preserve WGSL relaxed ordering; do not infer a memory
fence or insert unrelated synchronization. Keep group initialization/barriers
with the existing workgroup owner. The optional atomic-software route executes
fresh emitted HLSL through official DXC/SPIR-V on explicit CPU Vulkan; it does
not establish D3D12 or physical GPU support.

Explicit depth sampling consumes the common image operation and existing handle
flags: ordinary sampling uses an integer WGSL LOD and scalar result; comparison
sampling uses a comparison sampler, saved depth reference and explicit mip zero.
Keep 2D optional offsets attached to their validated constant IR/DAG identity,
never a replayed expression or runtime local. Cube variants have no offset.
Target-native packed coordinates must retain the separately clamped array layer.

Implicit sampling is already fragment/derivative-validated by the shared owner.
Use the native implicit sample/comparison methods with saved operands and checked
constant offsets; do not substitute mip zero or recompute gradients on the host.
Depth returns scalar values; ordinary sampled textures return four components.
