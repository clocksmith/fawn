# Concrete-suite correction before any candidate knowledge or handoff

The initial source manifest1d6d86fbb19624b2152521a58a1a444b8f678339baccfebd4abb2b25867bfd08
and receipt82718c43720fbcd93fc330a94682b8c55d415c9f23f3538545548a06058d97b4
are preserved byte-for-byte with their28 bound files under
sealed-v1-before-witness-loop-fix/. Their actual inventory was563 fixtures,
including only six1x1 witness scenarios, despite the README's intended18.

A final source-only inventory check immediately after that seal exposed a
misindented return in the witness generator. It exited the outer grid loop after
1x1. This was an evaluator preparation defect, not a compiler observation.
The parent was notified before any candidate handoff.

The return now occurs after all three grid shapes. The oracle self-control now
asserts exactly18 scenarios, exactly three source texts and both range variants.
The twelve newly included scenarios have the same prewritten source-level
stencil formulas and packing model; no expected value or admitted language rule
was changed. The intended CLI witness name is now present as well.

The superseding inventory contains575 cases. Candidate implementation inspection,
build/link/load/execution and author disclosure remain NONE. This correction
retains pre-inspection independence but does not retroactively validate the
first seal's missing scenarios. Preserve both records.
