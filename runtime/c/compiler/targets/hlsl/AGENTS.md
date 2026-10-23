# HLSL target owner

Keep this private adapter on the shared C typed IR. Do not duplicate admission,
constant semantics, module closure ownership or the parser. `hlsl.h` owns the
borrowed-module/output contract; this directory does not select a public target.

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
- Subgroups use explicit Shader Model 6 wave intrinsics, not workgroup memory or
  host execution. Extend closed builtin/input mappings deliberately; unknown
  operations reject before publication. Dispatch metadata follows retained b0,
  space7, and conflicts reject rather than silently changing the native ABI.
- Run `make check host-control library`. These establish native C compilation and
  bounded developer controls, not DXC, DXIL, D3D12 or physical GPU acceptance.
