# D3D12 C owner

- Read the common C guide and this directory's charter. Native WebGPU object
  validation and shader meaning remain outside this backend-private ABI.
- Use Windows SDK C declarations in `windows_*.c`; do not construct replacement
  COM vtables, spoof `_WIN32`, or call host controls an SDK/driver check.
- Exact ordinal selection stays the zero default. WARP requires its explicit
  selector and software opt-in, uses EnumWarpAdapter, and verifies the actual
  DXGI software flag. Never retry another adapter after selection failure.
  Hosted WARP execution is CPU software evidence, never physical GPU evidence.
- Keep evidence bound to its tested source. The [WARP developer receipt](DEVELOPER_RECEIPT_WARP.json)
  records actual hosted CPU direct-compute/readback/cleanup at `159b58a18`.
  It does not qualify other fixtures, public D3D12 integration, later source,
  physical hardware or performance. Preserve the earlier SDK-only receipt.
- The [Miles developer receipt](DEVELOPER_RECEIPT_MILES_D3D12.json) separately
  records six physical private-backend controls on its named AMD adapter/driver
  at the same source. Do not transfer that evidence to public integration,
  presentation, later source, other devices or performance. Preserve its first
  infrastructure failure and source/tool/binary identities.
- The serialized context is the sole submitted-command retirement owner.
  Execute first publishes that ownership; failed signal/wait leaves every
  obligation intact. Only observed completion or terminal device loss settles it.
- A buffer, texture or timestamp heap is exclusive to one recorded or unresolved command. This deliberately
  rejects cross-command resource reuse until completion, rather than pretending
  to support arbitrary overlapping command lists or WebGPU usage scopes.
- Every default-buffer/texture operation begins/ends at COPY_DEST; upload/readback heap
  states stay fixed. Descriptor heaps and allocators survive their whole submission.
- Test host ownership at the private typed native-call link seam, not a simulated
  driver or shader interpreter. Keep actual Windows readback as a separate fixture.
- Texture formats, whole-subresource copies and immutable views remain an explicit
  subset. Share checked footprint arithmetic with the common leaf, but keep D3D12
  alignment/format admission here. Never publish readback padding as initialized
  by a texture copy; it must already be initialized or absent.
- Sampled/storage descriptors and static samplers are pipeline/command-owned, not
  a second resource registry. Dynamic views/samplers need a real consumer before
  broadening the boundary. Preserve historical source seals when extending it.
- Graphics and compute share root layout, binding validation and pipeline lifetime.
  A rejected draw must not record a clear or transition; allocate RTV, optional DSV and
  shader descriptor heaps first. Never infer full initialization from draw coverage.
- Timestamp slots require an actual EndQuery before resolve. Track speculative slot
  initialization on the existing command, publish at completion, and reject partial
  readback into uninitialized bytes. Zero frequency is failure, never a fallback.
- Vertex/index input uses allocation-bounded native views. Direct vertex/instance
  spans are checked; index contents remain GPU data, not a fabricated CPU proof.
  Reject aliases across shader/vertex/index roles before transitions or retention.
  D32 clear/load uses the same texture initialization and command retirement owner.
- Vertex location semantics are TEXCOORD, matching the current C HLSL emitter,
  not the retained Zig bridge's ATTR convention. Source agreement alone is not
  DXC compilation, SDK linkage or native-consumer admission.
- Indirect arguments stay GPU-owned. Validate the record's initialized allocation,
  range, alignment, aliases and resource budget before native recording. Dispatch
  dimension proofs belong upstream; never replace them with a CPU read or invented
  dimensions. The context owns its native command signature through retirement.
- Uniform CBV, structured SRV and structured UAV namespaces use the portable
  descriptor-kind owner. Uniform ranges include initialized physical padding,
  obey native alignment/size limits, and share graphics/compute resource leases.
  Do not reinterpret b-registers as t-registers or widen a logical binding silently.
