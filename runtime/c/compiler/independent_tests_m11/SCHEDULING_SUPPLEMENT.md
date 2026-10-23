# Pre-candidate independent harness supplement

2026-10-23, before any M11 candidate inspection or execution. The original
CASE_SEAL.sha256 and its exact source/input/expectation matrix remain unchanged.

The frozen round-robin, reverse, seeded shuffle and bounded-burst policies remain
required. Small execution fixtures and all synchronization controls are also
run at one target-instruction quantum. The larger unchanged witnesses may use
observational event scheduling: execute only invocation-local/register/Function
memory instructions until the next observable Workgroup or StorageBuffer access,
barrier, or a maximum of 64 pure target instructions. This bound prevents a long
barrier-free local loop from defeating fairness or hiding a stalled invocation.
Calls use the same suspended per-invocation stack, and stop at every nested
observable access and barrier. Source order is never rearranged. Both target
instruction and scheduling-event counts are reported; this is not an invocation-
at-a-time interpreter or a different numerical/source oracle.

Instruction and event granularity must match exact outputs and detect the same
controls on the small fixtures. Dynamic rendezvous includes actual static target
instruction, current helper-call chain and active loop-iteration stack. A separate
hand-assembled validator-valid helper mismatch and a distinct loop-iteration
mismatch demonstrate that static-opcode-only rendezvous is insufficient.

Accepted M10 native overlap ownership remains unchanged: full-capacity overlap is
a caller precondition, not a new promised rejection. The plan's alias/span checks
mean the cases actually supported by the public contract, not invented behavior.

Current native private-size changes are explicitly enumerated in native/. No
historical tests, statuses, public headers or original failure receipts changed.
