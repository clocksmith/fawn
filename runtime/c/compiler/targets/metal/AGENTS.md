# Metal target owner

Use the common C guidance; do not fork WGSL parsing, validation or layout here.
This directory consumes the private typed module after whole-module validation.
Keep type/resource/local/value IDs distinct. Derive offsets/strides from shared
layout records. Do not use native C++ structure layout as WGSL buffer layout.

- Storage and uniform buffer accesses, including runtime-array tails and atomics,
  clamp every index: `min(uint(index), last)`, where last is a fixed array's or
  vector's extent minus one, or for a runtime tail (bytes - tail) / stride - 1 when
  at least one element is bound and 0 otherwise, emitted branch-free as
  `max((bytes - min(bytes, tail)) / stride, 1u) - 1u` over the slot-30 size table
  (`ctx.rN_bytes` in ABI12, `ctx.sizes[slot]` in ABI10). A negative i32 index
  reinterprets as a large uint and clamps to last. A clamped access lies inside
  the declared extent and runs unconditionally: no zero load, no dropped store, no
  access guard; each storage atomic forms its pointer in its own scope. Metal has
  no hardware buffer bounds, so this relies on admission: the runtime rejects a
  dispatch whose binding is below the reflected minimum (the fixed size, plus one
  tail element), as fixed-size paths already required. WGSL robustness permits
  any in-bounds element.
- Workgroup, private, function, pointer and value roots keep one guard per access:
  guard signed indices and each indexed subobject before forming the access;
  loads initialize zero and invalid stores discard.
- Direct 32-bit scalar (i32/u32/f32) storage/uniform accesses are one statement at
  the access site that indexes the buffer as words (each byte term divided by
  four); a term that is not a whole number of words keeps a byte-offset pointer.
  Their scalar helpers are emitted only when an aggregate helper reaches them.
- A scalar IR constant has no value local: every use writes its literal
  (`as_type<float>(0x...u)`, `0x...u`). Composite constants keep their
  constructors.
- A value local drops its zero initializer only when `assignment.c` proves it:
  its first assignment is unconditional at its own statement (`metal_define`) and
  every reference follows it in the same structured region or one nested inside.
  Branch arms, cases, short-circuit sides and loop header/body/continuing are
  separate regions. Any other value, and any function past the bounded state,
  keeps its zero initializer. Never drop an initializer by pattern or opcode alone.

Keep image handles as resolved resource/formal identities. Writable MSL textures
must pass as parameters, not local copies. Integer coordinates, array layers,
mips and samples need independent guards; sampling coordinates/LOD retain sampler
semantics. Preserve buffer slot 30 and the separate native texture/sampler table
policy in README.md. Compare new image mappings with Apple's actual MSL methods,
not another target's packed coordinates or component-width rules.

Use `make check host-control image-host-control` for quick developer feedback, with output in /tmp.
The host controls erase Metal address spaces and attributes, so they establish
neither MSL validity nor GPU behavior. Keep that limitation explicit. Real Metal
SDK/native consumption and independent numerical qualification remain separate.

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

Graphics consumes shared typed leaves and formal/member identities through
`../stage_view.h`; never replay source annotations. Vertex location inputs use
attribute buffers, interstage locations use user attributes, fragment outputs use
color attributes, and builtins are separately typed arguments. Preserve resource
closure/size tables and call effects. Invariant needs a real compiler policy, not
an ignored attribute. Test new mappings with the emitted-operation control while
keeping Apple SDK acceptance explicitly unrun.

ABI12 argument allocation belongs to one entry-specific typed plan. Preserve
ABI10's explicit legacy mapping and workspace shape. Never derive dense slots
from source group/binding numbers or reallocate them in metadata adapters. Every
row belongs to an exact entry span; namespaces are independent, and runtime-tail
size_index equals its physical buffer slot. Shared helper code uses logical
ctx.rN_bytes facts set by each entry wrapper, never a global slot assumption.
Charge all new plan/selection work to existing budgets; no repeated uncharged
closure scans or additional allocator/work pool is permitted.


Atomic access uses the same path policy and immutable ordered operands as
ordinary memory: storage atomics clamp their indices; threadgroup atomic pointers
are formed only inside their guards. Keep native signed/unsigned atomic
types and relaxed ordering. A weak compare-exchange may retry a spurious failure
only while its observed value still equals the saved comparison; never replay
operands. Initialize selected workgroup atomic trees in place before the existing
barrier; never copy atomic-containing aggregates through by-value helpers. Keep
SDK source compilation and physical execution evidence distinct.

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

Logical all/any consume the shared unary builtin operand. Copy scalar bool;
map bool vectors explicitly to native reductions with scalar bool results.
Use the saved operand once; source effects already occurred before reduction.
