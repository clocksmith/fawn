# C compiler implementation style

Follow the repository's native adapter rules and the frozen milestone. This local guide records implementation choices, not expanded language or qualification claims.

- Use C11, explicit status/boolean results and caller-owned spans. Check lengths and capacities before indexing, casts and publication.
- Keep lexical, parsed, validated and target representations separate. Emission consumes typed binding identities and operations, never source spelling.
- Preserve source offsets through semantic lowering. Report the first admitted pipeline failure with a useful bounded message.
- Keep ownership visible: stack records in the library, file descriptors in the CLI with one cleanup path, no retained caller pointers or mutable globals.
- Prefer typed, bounded records over generic containers, callback frameworks or erased `void *` domain values.
- Use named small helpers for parsing, resolution, validation and emission. The emitter's local serialization macro only supplies an array length and propagates failure; it is not an IR or dispatch framework.
- Keep test evidence separate from implementation. Test semantics, error/output atomicity and real native consumers, not desired source snippets. Keep first failures when repairing implementation or classifying intentional contract expansion.
- Preserve MIT notices and imported-source identity. Do not mechanically translate production Zig or mutate historical experiments.

## Structured owner limits

The private syntax owns at most 16 preorder statements. Each if records then/else
ranges, and parsing rejects the fifth nested if at its original token. The typed
linear IR uses explicit if/else/end markers, not source text. One assignment needs
at most 13 IR instructions: three indices with three operations each, two loads,
one addition and one store. An if needs at most eight including its structural
markers. The instruction budget is therefore 1 + 16 * 13 = 209, including the
shared invocation-x value; IDs use instruction positions plus one, and the value
map has 210 slots including unused zero. Every append checks the instruction cap.
The existing 4,096-word serialized-output cap is still checked on every emission.

Validate and lower all source statements even after return. The emitter alone
suppresses unreachable operations, carries actual current labels through robust
access merges, and adds branches only to live blocks. Both-return selections
still emit their required merge with `OpUnreachable`. This preserves nested Phi
predecessors without ever adding a second terminator after return. Keep the public
header byte-identical; private workspace growth is measured, not hidden.
