# D3D12 C owner

- Read the common C guide and this directory's charter. Native WebGPU object
  validation and shader meaning remain outside this backend-private ABI.
- Use Windows SDK C declarations in `windows_*.c`; do not construct replacement
  COM vtables, spoof `_WIN32`, or call host controls an SDK/driver check.
- `windows_shader.cpp` alone adapts official `dxcapi.h`/`d3dcompiler.h` interfaces behind
  [the private C shader ABI](include/doe_d3d12_shader.h), with exceptions/RTTI off.
  Load only the explicit fully qualified compiler module, use fixed compile flags
  and pass the compiler-owned entry index verbatim. No CLI, include handler,
  handwritten COM ABI, WGSL interpretation or capability policy belongs here.
  Explicit compiler requirement501 selects compute FXC; legacy/default600 and
  native16/620 stay DXC. No retry/profile fallback or imported-source inference.
  Keep ID3DBlob and IDxcBlob typed; never cast between compiler interfaces.
  Copy the first diagnostic before releasing compiler objects; returned bytecode owns
  its blob and module. The native backend separately retains one reference per selected
  compiler module for its lifetime (published by compare-exchange, released after
  the context), so pipelines stop unloading and reloading it; this never selects
  or substitutes a module. Device open takes the FXC (compute profile 501) reference
  the same way, as Dawn loads its compiler with the device: the load cost moves to
  device creation, it is not removed. An open-time load failure is not a device
  failure; the first compile retries the same explicit module and reports its own
  failure. Restore caller fenv/MXCSR/errno/LastError before publication.
  SDK compilation/control evidence does not establish GPU execution.
- Exact ordinal selection stays the zero default. WARP requires its explicit
  selector and software opt-in, uses EnumWarpAdapter, and verifies the actual
  DXGI software flag. Never retry another adapter after selection failure.
  Hosted WARP execution is CPU software evidence, never physical GPU evidence.
- Hardware-class selection may skip software entries only before selecting a
  member; subsequent device/query failure never retries a different member.
  Device reopening uses exact LUID words and software identity, checked against
  both DXGI description and the created device. Missing identity fails closed.
- Keep raw feature/tier/VA/UMA queries separate from SDK guarantees and explicit
  implementation ceilings. Required queries fail construction. The 256 MiB buffer
  ceiling is validated allocation metadata, not free memory/residency; each
  allocation still owns its real failure. Never infer WebGPU CoreFeatures here.
- Preserve the compute 64-resource/16-sampler capacity with tier-limited kind
  counts. Graphics packs two 64-resource/16-sampler stages into 128/32 on the
  same descriptor-use owner; legacy 16 static samplers retain their ABI. Cumulative command resources use the existing
  growable use spans, not a fixed resource budget.
  Official-SDK CPU vtable controls prove call composition only, never a device.
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
  A held progress lease may observe its native fence/event outside serialization.
  The existing context object obligation protects those handles after a command
  shell retires; timeout parks its event registration, never closes it. Active
  observers cannot be collected or destroyed. Reuse parked registrations for the
  same fence value; drain terminal notifications before final native close.
  Execute first publishes that ownership; failed signal/wait leaves every
  obligation intact. Only observed completion or terminal device loss settles it.
- A buffer has one exclusive unsubmitted recording reservation. Known-fenced
  acceptance releases that reservation for later GPU use on the same ordered
  queue; each existing command retains its own buffer reference. A borrowed
  latest-accepted identity and initialization bit authorize future GPU recording,
  never CPU access or retirement. Keep completed initialization separate; clear
  either identity at retirement/discard only when it still names that command.
  Failed signal retains the reservation until known-fenced recovery. CPU access
  rejects both recording and pending use. Textures/timestamps retain exclusive
  recorded/unresolved ownership; this is not arbitrary overlapping usage scopes.
- The existing context may retain one immutable zero-filled UPLOAD buffer up to
  one MiB. Acquire returns a separate ordinary caller lease; preserve it across
  unlocked frontend initialization waits and cache replacement. Larger sources
  remain uncached. CPU writes reject shared zero sources; never use them for
  caller data. Copies still cover the whole destination allocation/padding.
  Context destruction drops only its cache lease before checking outstanding
  caller/command/event obligations; failed native close remains retryable.
- Caller-data staging may retain one ordinary UPLOAD page up to one MiB. Reuse
  requires exactly its cache lease and no recording/pending use. Busy/oversized
  requests allocate independently; no CPU write overlaps GPU use. Only a larger
  speculative page's OOM may retry the exact requested size. Keep immutable zero
  storage separate, copy only caller bytes, and release both caches at teardown.
- Shader-visible CBV/SRV/UAV descriptors come from context-owned 4096-entry
  heaps: each descriptor use borrows one contiguous range, rewritten before
  recording, and returns it only when its command is discarded, terminally lost
  or successfully retired. Device creation prepares the first heap (never the first
  submission) and keeps it for the context; extra heaps for
  overflow release when empty. Sampler/RTV/DSV/clear heaps keep their own owners.
- Storage-size tables (root CBV b2, space7) live in the same ring: each ring owns
  one 64 KiB UPLOAD arena of 256-byte slots, mapped once and released with the
  ring; device creation prepares the first ring's arena with its heap (never the
  first dispatch), overflow rings create theirs on first need. A use takes its table slots with its descriptor range
  from one ring and returns both together, so a CPU table write never overlaps GPU
  reads. Every storage row (raw or structured) carries its exact bound range size;
  other rows are zero. Never create a buffer per use for this table.
- Buffers whose queried allocation is at most 1 MiB are placed in context-owned
  4 MiB buffer-only heaps of their own memory class (64 KiB slots). Heaps and
  committed resources never set D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, so D3D12 zeroes
  them at creation. A buffer whose slots were never placed since CreateHeap, or a
  new committed resource, is known zero: UPLOAD and zeroed-READBACK construction
  skip their CPU zero writes. DEFAULT keeps its explicit deferred fill (skipping it
  moved the queue's first execution into the caller's first submission and was
  rejected). A slot placed once is never known zero again. Placement order is
  unchanged (first fit). Slots return only
  when the resource is released after retirement; keep one empty heap per class,
  detach live heaps at close, and never retry a failed heap/placement as committed.
  Device creation prepares the first UPLOAD heap with the descriptor ring (its cost
  moves from the first queue write to device creation); failure fails the open.
- DEFAULT zero fills are deferred on an exact per-device list and submitted, all in
  one command, before any later D3D12 recording or CPU read. Only a whole-allocation
  queue write (padding included) may replace a fill; partial writes materialize first.
- Queue writes record into one open per-device command (frontend `d3d12_writes.c`),
  which holds each copy's ordinary recording reservation. The next queue submission
  adopts it: `doe_dx_commands_order_buffer_writes` transitions every DEFAULT use out
  of COPY_DEST and back (copies into one COPY_DEST buffer are otherwise unordered),
  then the submission's own work records after the writes, under one
  ExecuteCommandLists and one fence. A READBACK destination cannot transition, so
  those writes execute alone first. Every other recording, CPU read, map and
  work-done drain submits the open command first; a second write into a logged
  destination also does, as do the 64th write and 16 MiB. A copy log (staging and
  destination leases) re-records the writes when an adopted recording fails, so a
  rejected submission or failed rebuild never loses them. Final device release
  discards a never-observed open command.
- The additive zeroed READBACK constructor finishes CPU Map/zero/Unmap before
  publication or any GPU use, unless D3D12 zeroed that memory at creation. Zero exactly resource Width, including requested
  padding, never committed-heap alignment slack. Keep ordinary READBACK create
  uninitialized and general host writes UPLOAD-only. Unmap ranges describe writes
  for tools; they are not a fence or permission for CPU/GPU overlap.
- Public read mappings of READBACK buffers lend the mapped allocation instead of
  copying it: lend has read's admission (initialized, no recording/pending use),
  keeps one Map open, and while lent rejects GPU recording, host reads/writes and a
  second lend. The frontend returns it on unmap, destroy, release, aborted delivery
  or refused admission; return is idempotent and final release returns implicitly.
- Every default-buffer/texture operation begins/ends at COPY_DEST; upload/readback heap
  states stay fixed. Descriptor heaps/ranges and allocators survive their whole submission.
  Only successful terminal retirement may transfer a closed native command
  allocator/list pair to the serialized context for reuse; at most four pairs are
  kept, reused oldest first. Reset allocator before list; propagate either
  failure without fallback. Discarded, unknown and lost work never enters this
  cache. Context teardown releases every idle pair.
- Test host ownership at the private typed native-call link seam, not a simulated
  driver or shader interpreter. Keep actual Windows readback as a separate fixture.
- Texture formats and immutable views remain an explicit subset. Whole-resource
  transfers retain their existing API; typed color-region transfers additionally
  admit initialized DEFAULT memory with real source-state restoration. Partial
  uploads/blits preserve existing initialization; they never establish full writes.
  Share checked footprint arithmetic with the common leaf, but keep D3D12
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

- Within a command, compatible read aliases share one buffer lease and aggregate
  CBV/SRV native state. Writable UAV aliases require disjoint byte ranges; reject
  mixed read/write classes before recording. Transition/restore each allocation
  once, retaining individual descriptor ranges. Resource budgeting counts unique
  allocations, never descriptor aliases. Unsubmitted reservations remain exclusive.

- Growing use spans stay command-owned. Reserve all required capacities before
  native recording or publishing counts, initialization or leases; failed
  preparation retains every allocated span for retry/discard. Exclusive objects
  carry stable owner indices, never cached addresses into movable use storage.
- Pipeline use is shared-read: append one existing reference per successful
  record, then drop each once. No repeated cumulative pipeline scan, auxiliary
  registry or new lifetime counter. Keep genuine OOM distinct from validation.

- Dynamic samplers are immutable values copied into each existing command-owned
  descriptor use, with one separate sampler heap and root table. Preserve legacy
  static descriptors. Compiler handle metadata selects sampled classes versus
  exact storage formats; READ storage is an SRV with logical STORAGE admission.
  Compatible read texture aliases share one lease/transition; writable aliases
  reject before any native mutation. Do not infer a public usage bit from SRV.

- The additive procedural render path owns two typed stage maps on the existing
  pipeline, never independent shader owners. Root tables carry VERTEX/PIXEL
  visibility. Pack both stages into exactly one resource heap and one sampler
  heap per descriptor use, with explicit table offsets; never remap registers.
- Attachment operations belong to the whole pass, including empty/zero draws.
  Eager real zero clear on discard keeps initialization with the existing command
  texture-use owner. Prepare every heap before recording; failure preserves
  earlier command records. Preserve the original static graphics ABI and compute
  capacity while extending this first graphics checkpoint toward full parity.

- Whole-pass vertex inputs reuse the typed format/geometry owners. Preserve all
  41 canonical formats, SDK format queries, public min(4, format-bytes) alignment,
  holes, explicit empty-slot identity and zero strides. A native format failure
  remains typed unsupported; never CPU-decode vertices or rewrite shader source.
- Vertex/Instance fetch extents are checked before recording. Repeated vertex and
  shader-read roles aggregate native states and retain one allocation lease;
  reject a mixed UAV role before mutation. Indexed and indirect whole-pass
  records require the upstream common V2 argument/index-scan GPU bounds contract;
  allocation-bounded IA views alone do not prove user index values.

- Native VS/PS composition uses the compiler-owned 32-bit interstage mask only
  after selected interface admission. Keep the old shader options/compile ABI
  intact; the additive projection descriptor rejects missing/wrong-stage/reserved
  input through the same DXC/host-state owner. Zero is a valid position-only mask.
- Pass the compiler's exact define with a bounded uint32 literal. Both graphics
  profiles explicitly use verified `-pack-prefix-stable`; compute remains outside
  this policy. Never rewrite shader text, infer WGSL fields or cache DXIL without
  its entry/projection identity. Actual SDK construction/pixels remain separate.
