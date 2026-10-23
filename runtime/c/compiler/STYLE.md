# C compiler implementation style

Follow the repository's native adapter rules and the frozen milestone. This local guide records implementation choices, not expanded language or qualification claims.

- Use C11, explicit status/boolean results and caller-owned spans. Check lengths and capacities before indexing, casts and publication.
- Keep lexical, parsed, validated and target representations separate. Emission consumes typed binding identities and operations, never source spelling.
- Preserve source offsets through semantic lowering. Report the first admitted pipeline failure with a useful bounded message.
- Keep ownership visible: stack records in the library, file descriptors in the CLI with one cleanup path, no retained caller pointers or mutable globals. Optional reflection is a nullable path, not a default filename; only a successfully acquired temporary or published artifact is eligible for cleanup.
- Prefer typed, bounded records over generic containers, callback frameworks or erased `void *` domain values.
- Use named small helpers for parsing, resolution, validation and emission. The emitter's local serialization macro only supplies an array length and propagates failure; it is not an IR or dispatch framework.
- Keep test evidence separate from implementation. Test semantics, error/output atomicity and real native consumers, not desired source snippets. Keep first failures when repairing implementation or classifying intentional contract expansion.
- Preserve MIT notices and imported-source identity. Do not mechanically translate production Zig or mutate historical experiments.

## Structured owner limits

The private syntax owns at most 16 preorder statements and 64 expression
nodes. Each if records then/else ranges, and parsing rejects the fifth nested
if. Expression depth is at most 16: leaves count one, binary/max nodes add one
to the deepest child, and parentheses add one without allocating another node.
Indices keep the separate bounded M2 grammar. The exact leading invocation-x
alias retains its old zero statement/node accounting; other lets count normally.

The typed linear IR uses explicit if/else/end markers, not source text. Each
expression node needs at most four instructions (an index with at most three
instructions plus one robust load). A statement adds at most eight instructions:
an if's three-operation left index, right constant, comparison and three control
markers. An assignment adds at most four for its destination index and store;
a let adds none beyond its expression, and return adds one. Child statements
and expressions are counted separately. Thus the checked safe capacity is
1 + 64 * 4 + 16 * 8 = 385 instructions, including shared invocation x.
IDs use instruction positions plus one, with 386 value-map slots including
unused zero. This conservative capacity is not a claim that every slot is
reachable by a grammar-valid program. Every append and every serialization
checks its bound; the public 4,096-word output cap is unchanged.

Scope records retain evaluated typed value IDs, not expressions to replay.
Save/restore the visible record count around each brace-delimited block. A new
local enters scope only after its initializer; sibling scopes cannot leak
values. Preserve legacy alias lookup rules, but reject new-local shadowing of
visible user declarations. Resolve max and explicit type annotations against
visible values before treating their spellings as builtins.

Decimal syntax retains exact numerator/denominator records. Materialization
normalizes that rational with checked integer operations, forms a 24-bit
significand, and rounds by comparing remainder against denominator minus
remainder, breaking ties toward an even significand. There is no locale-sensitive
or host-floating conversion. A float leaf is not a runtime operand until bound
to a local; reject constant-only f32 arithmetic/max subtrees instead of evaluating
abstract expressions at binary32 precision. Target arithmetic follows tree and
source order as separate operations; there is no reassociation or contraction
pass. Max lowers to a typed comparison and select.

Validate and lower all source statements even after return. The emitter alone
suppresses unreachable operations, carries actual current labels through robust
access merges, and adds branches only to live blocks. Both-return selections
still emit their required merge with `OpUnreachable`. This preserves nested Phi
predecessors without ever adding a second terminator after return. Keep the public
header byte-identical; private workspace growth is measured, not hidden.
