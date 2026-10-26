# DXIL compatibility and qualification limits

## Shared source frontend, not a duplicate parser

`dxil_analyze.c` now adapts the canonical C compiler into native IR.
`dxil_source.c` adapts the same compiler's production HLSL output into DXC.
The old standalone parser implementation has been removed. Source `for`,
forward struct references, composite/negative initializers, typed matrix
indexing, override IDs/defaults, graphics interfaces and semantic validation
are owned by the canonical compiler. See
[source-integration.md](source-integration.md) for contracts and test commands.

## Retained native backend placeholders

`dxil_native_walk.c` preserves the behavior of retained Zig
`emit/dxil/emit_dxil_native.zig`: references and constructs produce zero,
user calls do not execute their bodies, assignments evaluate the RHS without a
store, and branch/loop/switch bodies are walked without executable control flow.
Address/dereference expressions fail explicitly. These are shared backend
capability limitations, not C-only migration regressions. A structurally valid
native container does not establish a correct executable shader.

The native projection retains explicit `dxil_ir.h` table budgets and checked
source/workspace/depth limits. The canonical source owners do not promise
unbounded admission matching Zig's growable lists. Invalid native identities
return errors rather than copying Zig traps.

The bounds view shares canonical extent/constant facts and read-only guarded
loop facts. It does not infer a second semantic graph. Broader retained interval
policies require actual byte/error comparisons; neither a source inventory nor
the focused differential corpus proves identity for every program. Concrete
integer folds through immutable aliases and typed integer/boolean identities
reuse the shared numeric owner. The retained reference is deliberately corrected
for large runtime shift counts and discarded effectful call subtrees; these
comparisons are not against an unchanged historical oracle. Production HLSL uses its canonical typed owner and checked memory-access
lowering instead.

## Caller-built IR HLSL boundary

`dxil_hlsl_emit(DxilIrModule*)` is a checked representation serializer, not another
WGSL semantic engine. Graphics wrappers, clip-distance output snapshots, typed
numeric/texture helpers, real-global atomic helpers, reference index snapshots,
and runtime-array write guards are implemented. Floating-point text shares the
finite decimal owner; float packing shares the production target definitions.
Structural table/range/identity validation precedes output and also protects the
native emitter. These APIs observe current caller IR, never a cached source copy.

The retained typed-structured-buffer model for a structure with an unsized tail
and its `arrayLength` formula is preserved, not certified as valid device layout.
Formal-pointer-only atomic targets lacking a concrete global origin reject
instead of using HLSL copy-in/out parameters as if they named shared atomic
storage. The source compiler's production target owns full pointer specialization
and byte-layout lowering. See [texture-serialization.md](texture-serialization.md)
for the direct-IR texture shape contract and deliberate fail-closed differences.
Extra conservative pointer captures may change text while preserving snapshots.
Runtime pointer validity and direct-store index/RHS effects are captured once;
these explicitly correct retained lowering that could replay an index or skip
RHS effects on an invalid store. They are not described as unchanged text parity.

## Retained source differences

The shared C frontend admits valid forward-struct private initializers and
composite constant arithmetic that retained Zig rejects. It rejects duplicate
`@id` declarations that retained Zig accepts. These are explicit admission
comparisons, not silently counted as parity matches or reasons to regress the
canonical frontend. Native table/depth/work budgets also remain explicit.

## Deliberate host differences

- Diagnostics are caller-owned, not thread-local. The old allocation-free
  compiler ABI is unchanged; host allocation is an explicit companion boundary.
- Integer/container narrowing and native identity errors fail visibly instead
  of copying Zig panics. Native/direct output budgets remain 256 KiB.
- DXC temporary ownership uses an exclusive per-call directory, not a timestamp
  nonce. Filesystem messages include host error detail. Both output streams
  have bounded retention, while the process drains excess bytes.
- Caller output is staged and published only on success. Cleanup errors cannot
  overwrite an earlier compiler/process error.

## External qualification

Controlled-child tests establish argument, file, lifetime and error behavior;
they do not establish that DXC accepts a shader. The Windows process runner's
separate real-DXC receipt does not automatically qualify the new whole-source
adapter or its filesystem boundary. Windows execution, real DXC source
compilation, D3D12/device behavior and release admission require fresh receipts
for the actual integrated path. No GPU performance or Doe-versus-Dawn claim
follows from these compiler tests.
