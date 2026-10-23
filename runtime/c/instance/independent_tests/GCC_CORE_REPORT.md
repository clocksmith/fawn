# Independent GCC-only instance-core result

PASS for the exercised standalone C core only. Native C-instance migration
acceptance remains OPEN pending separate fixed source handoff, fresh four-provider
native builds, actual retained consumers and native ownership/build audit.
No default, physical GPU, package/browser or complete-migration claim is made.

## Bound sources and test independence

GCC core handoff seal:70dc3a31e083b213fdecd27b3f81511e973fb2c341bd6c33edae7c7daa9d9472.
All supplied source/author-artifact manifests verified before copying. Source
builds used the copied exact core, not moving native/M10 files. Every bound source
still matches both copy and author tree after all tests; see GCC_CORE_RESULTS.json.
The frozen public header remains identical to the reviewed contract header.

Concrete cases predate implementation (CASES_SEAL.sha256). Executable fixture
sealV5 predates core source handoff. Prior fixture versions, failed baseline
expectations and documented repairs remain intact. Private count-field binding
and extra status/active/ordering code were written after core inspection and are
honestly labelled supplements implementing presealed expectations, not blind code.

## Passed independent evidence

- Strict GCC-only C11 instance and accepted-future archives; strict C++17/no
  exceptions/no RTTI standalone caller. One source-built copy of each owner.
- Validation/version-before-nested-access, original allocator identity snapshot,
  distinct object/wait state and vtable identities, exact pointer/extent/alignment
  pairing, null-stateless allocators, real queried queue storage and4096 alignment.
-257 balanced lifetimes, non-owning copied queue lease, real final callback-driven
  caller/event/payload destruction on joined publishers and actual wait/pump calls,
  exact cleanup order and no post-free observer reference.
- Guard floor1 versus nonfinal undo, zero/max guards under reduced-limit3 and
  normal UINT32_MAX, eight threads with10,003 balanced retains/releases each.
- Overflow/OOM leave original flags intact; invalid IDs preserve partial flag
  reset [0,0,0x33]; duplicates deliver once; timeout preserves the obligation;
  same-thread callback wait differs from a joined other-thread wait.
- Real callbacks obey issuance/lowest-ID order, outer boundary deferral and fresh
  recursive pump boundary. Wait input C,A,B still dispatches A,B,C.
- Four platform setup failures fully roll back; all cleanup-required setup,
  condition/mutex/unlock uncertainty, pending queue and actually-active queue
  finalization terminate before forbidden label/free. Actual active fixture
  retires its completion first, so BUSY is not merely a pending-node result.
- Every accepted wait status0..13 maps to correct WGPU Success1/TimedOut2/Error3
  while copying exact outputs from the real accepted queue. Unexpected requirements
  and process statuses fail diagnostically rather than claim success.
- Full GCC/C++ crossing layout outputs agree, with real C++ function/by-value
  return calls. Frozen-header GCC/Zig cimport layout probes also agree.
- ASan+UBSan and TSan public C/C++ runs pass with both instance and future C owners
  instrumented. Leak detection is explicitly disabled due the known ptrace limit;
  there is no LeakSanitizer result and no retained-Zig sanitizer qualification.
- Explicit author-prebuilt archive consumption passes separately, never substituted
  for independent source builds. Developer make-check reproduction passes as an
  additional labelled result, not the oracle.

Normal public runner:37 commands; ASan18; TSan18; author-prebuilt30. Private count
runner adds14 compile/control/fatal commands; status supplement adds6. Original
future developer and independent53-command regression suites and callback developer
checks also passed earlier. Exact outputs, compiler versions, commands, timings,
archive identities and first failures remain in named JSON receipts.

## C ownership audit

The C struct has the magic prefix, one atomic reference count, a pointer to the
single accepted queue, copied config and original extent/alignment. The owner
queries queue storage requirements, checks arithmetic, allocates once, observes
before initialization and publishes only after successful real queue init.
No guessed queue array, extra queue/count, Zig-owned shell or hidden scheduler.

Retain is relaxed CAS with zero/max guards; ordinary release uses acq_rel CAS
with relaxed failure and existing guarded floor1; undo independently rejects<=1
and ignores external count. Finalization snapshots config/size/alignment, requires
successful queue teardown, removes label, invalidates magic and frees without
rereading storage. Wait takes a full-call reference, copies typed records through
the copied distinct wait allocator, returns all output flags, frees temporary
storage before releasing the final call lease. Capability outputs remain empty.

No C-core implementation defect was observed. This does not substitute for the
still-unreviewed native hook binding, copied QueueView adapter, loss/pipeline/
surface consumers, trace identity, build closure or Z/F/FC/FCI regressions.
