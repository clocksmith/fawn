# M7 amendment 1: empty module declarations

Appended and independently reviewed on 2026-10-23 before implementation of this
admission. The original MILESTONE_7.md and CONTRACT_7.sha256 remain unchanged.

The first developer witness pass found that four of the five frozen unchanged
inference shaders contain a semicolon after a struct declaration. The frozen
witness acceptance therefore requires an omitted generic WGSL grammar rule, an
empty module declaration. Preserve that first rejection receipt; this is a
contract omission, not evidence that the old M6 parser violated its own subset.

Only the new job capability admits standalone `;` between module declarations,
including before/after a struct, resource or entry declaration. This is generic
empty-declaration syntax, never recognition of a witness name or source text.
Each semicolon still consumes its source byte/token budget, but it is not a body
statement or scalar expression and receives no such charge. Existing mandatory
module/type/entry validation remains: a semicolon-only source is not a valid
compute module. Semicolons inside malformed declarations do not become valid
empty module declarations.

The fixed ABI1/v2 capability adapters retain their previous grammar and failures.
Binary-only/reflection-v2 CLI use the job capability and thus admit this rule;
legacy --reflection stays ABI1. No other M7 limit, semantic rule, public layout,
reflection schema, artifact publication or acceptance requirement changes.

Independent acceptance adds job admission, retained legacy rejection, exact
source/token budget and invalid-placement/missing-entry cases. Old receipts and
negative expectations remain intact and any intentional lifts are named.
