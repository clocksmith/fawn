# Independent futures evaluation: expectations frozen before candidate review

Frozen 2026-10-23; independent evaluator has read the public draft contract/header,
retained owner, native callback/lifecycle contracts and wrappers, not candidate C.
Contract SHA256: d5c6432492adc2525e64ae66ad999a0f68a1d365e5a5f3acd5917b3aeb19b9e9.
Header SHA256: 0e4d9d8c7db3edca4303aa1fcaa126eaf97d0dcf4f8dea0cf34e5d5e3903f223.
Retained owner SHA256: be44acc738915f39412cfc4108f8204e31acbe59551fbc92c613d67d9ad59e92.
The six existing Zig tests are separate source characterization, never a linked
oracle or hidden fixture dependency. No implementation has been read or executed.

## Frozen case inventory and independently predicted outcomes

1. Requirements/version: ABI 1 reports positive power-of-two alignment and size;
unsupported versions and NULL outputs reject without changing provided output.
2. Queue/completion init: invalid version, NULL storage/out, insufficient size,
misalignment, invalid mode reject; supplied handle output becomes NULL. Guard
bytes outside allocation remain unchanged. Valid malloc/aligned_alloc storage
can initialize, deinitialize and initialize again. Declared arrays are not used.
3. Failed mutex/condition-attribute/setclock/condition initialization returns
SYSTEM_ERROR, rolls back only successfully initialized resources, leaves handle
NULL and permits reuse. Faults are injected at linker symbols, not source edits.
4. IDs start sequence 1, increase, have clear high bit; two owners never collide;
legacy IDs set high bit. Reinit receives a new instance namespace, never reused.
5. Registration rejects zero, foreign, future IDs and active duplicates; BUSY
queue/completion deinit does not alter the live obligation. Reuse uses new ID.
6. An issued but absent ID is complete; retired ID remains complete. Zero-count
wait succeeds; unknown input clears visited flags only, leaves later flags intact.
7. Unready registered IDs poll TIMED_OUT, stay registered and eventually settle.
Timeout is not retirement; pending deinit remains BUSY.
8. Modes 0 and 3 dispatch synchronously on publisher thread; 1 only in wait; 2
in wait or owning pump. Other queue cannot dispatch; ownerless mark-ready calls
callback immediately. NULL dispatch readiness rejects, manual retirement works.
9. Reverse registration/publication and shuffled wait spans deliver lowest ready
eligible ID at each serial selection. Duplicate wait entries cause one dispatch.
10. A wait containing an absent complete ID and a ready registered ID dispatches
the latter before returning success; all completed flags then match retirement.
11. Pump boundary is issuance, not readiness: callback can ready older ID and it
runs in same pump; newly issued follower does not run until later pump.
12. Recursive pump is a fresh boundary and can deliver newly issued follower.
13. Same-thread self-wait succeeds while another thread poll stays TIMED_OUT
until finish. Manual begin cannot begin twice; no lock remains across callback.
14. Callback finish/deinit/free is legal; outer pump, wait and immediate publish
must not touch completion afterward. ASan explicitly exercises all three paths.
15. Callback may finish/free last node, then reentrant queue deinit still BUSY;
callback may reenter issuance/wait/pump. Outer call return permits later deinit.
16. Callback without finish remains registered/delivering; no repeat dispatch;
other-thread wait remains pending and deinit BUSY. Explicit later finish settles.
17. Infinite waiter is observed entering a condition wait before a publisher
publishes mode 1 readiness; waiter wakes, dispatches once, returns success.
18. Finite waiter and external finish use observed wait-entry handshake, checked
thread creation/join, and yield success when finish wins without timeout.
19. Two concurrent pumps overlap callbacks through a pthread-condition gate;
each eligible node dispatches once, without assuming global callback ordering.
20. A blocked callback and concurrent wait/pump/readiness are genuinely overlapped;
other-thread wait stays pending for delivering node until its finish. All threads
join; queue destruction while callback has freed node remains BUSY.
21. Concurrent ID issuers receive unique monotonically partitioned identities;
no unsupported registration/publication/free race or stale pointer is attempted.
22. Monotonic finite wait uses one absolute deadline across spurious wakes;
zero timeout may dispatch; callback time can consume deadline without preemption
and scan-complete wins after callback. UINT64_MAX uses indefinite condition wait.
23. Injected CLOCK_MONOTONIC failure/condition failure reports SYSTEM_ERROR;
near native time_t maximum deadline addition returns TIME_OVERFLOW without wrap.
24. Injected ETIMEDOUT while another thread settles before lock reacquisition
still returns TIMED_OUT; later poll sees complete. No fabricated lost completion.
25. Agreed explicit test-only small namespace endpoints exercise last valid ID,
EXHAUSTED, unchanged outputs and saturation, not billions of operations.
26. Independent strict C11 and C++17 -fno-exceptions -fno-rtti consumers use only
public header/static archive. Public ABI enum values/layout and C linkage agree;
archive/source provenance, symbol list and undefined deps show no Zig dependency
or allocator use. Caller allocates/frees; library does not.
27. Source-bound optimized, ASan/UBSan and TSan checks run once where installed
runtime supports them. Runtime probe failure is reported, not called a pass.
28. Source-only portable suite plans/imports/smokes after packaging without a
second full campaign. Exact source manifests and commands bind all receipts.

## Rules and stopping conditions

Use independent static archive compilation, never candidate test runner or Zig.
Freeze candidate commit and source hashes before building. Preserve first failures,
then report exact failing case and ask parent for corrected sealed candidate.
Do not weaken expectations after failures. Contract changes require a separate
explicitly recorded review, never retroactive receipt edits. Original files and
prior receipts remain unchanged. Only evaluator evidence directory is writable.
No sleeps establish concurrency; pthread condition handshakes/barriers and joins
establish overlap, bounded subprocess timeouts merely catch hangs. The suite does
not claim exhaustive concurrency, stale-pointer safety, GPU execution, payload or
instance/device lease qualification, production integration or performance.

## Attribution

Original Doe owner and public API are MIT, copyright 2026 Doe contributors.
New evaluator tests are original independent tests derived from the above public
contracts and retained source semantics, not copies of candidate implementation.
