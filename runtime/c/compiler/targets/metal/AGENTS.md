# Metal target owner

Use the common C guidance; do not fork WGSL parsing, validation or layout here.
This directory consumes the private typed module after whole-module validation.
Keep type/resource/local/value IDs distinct. Emit byte-addressed buffer access
only inside independent bounds guards, and derive offsets/strides from shared
layout records. Do not use native C++ structure layout as WGSL buffer layout.

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


Atomic access uses the same typed path guards and immutable ordered operands as
ordinary memory. Form device/threadgroup atomic pointers only inside those guards;
storage also proves four-byte extent/alignment. Keep native signed/unsigned atomic
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
