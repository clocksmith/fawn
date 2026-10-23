# Independent fixture repairs

## Before native execution, 2026-10-23 05:47 UTC

The v1 seal and original native fixture/driver are retained in pre-native-fixture-v1. A review against installed Zig std.debug documentation found that std.debug.assert is optimized away in ReleaseFast. Replaced only those native invariant assertions with an unconditional check that panics on false, strengthening rather than relaxing frozen acceptance. No native candidate execution or native callback adapter inspection preceded this repair. C source evaluation had already passed and is unchanged.

Removed an unsupported -Dstrip=false option from the canonical build recipe after checking the accepted baseline build.zig option list. It is build plumbing only, not an expected-output or requirement change.

## Stateless allocator supplemental obligation, before candidate2 inspection

Candidate1 source review identified the adapter's state.? non-NULL assumption as
unjustified for standard allocators with an unused undefined ptr. Requirements
already froze original allocator state/vtable semantics. Lead-approved proposed
repair is state orelse undefined, retaining the exact vtable and all meaningful
stateful identities without fabricating a pointer or inspecting undefined source
state. The independent evaluator adds an owned deferred std.heap.c_allocator case
for wait/pump with actual final native destruction and no observer lease. This is
a transparent contract-derived coverage supplement after candidate1 source review,
not represented as blind original v1 coverage. It is sealed before candidate2
inspection/calls. A lucky non-NULL pointer value is never an oracle for undefined
state validity; source review remains decisive. Original v2 native fixture is
retained in pre-stateless-fixture-v2, with no existing case/value weakened.
