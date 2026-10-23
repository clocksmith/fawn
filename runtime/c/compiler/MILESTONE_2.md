# C compiler milestone 2: structured neighbor sum

Frozen before implementation on 2026-10-23, after accepted milestone 1 at
`25c41ee682a563523a123268af3ee2066c537b38` and its portable-hash repair `37d98fe`.
This extends the admitted language; it does not revise historical acceptance.
The version-1 public header, API failure rules, reflection and CLI contract stay
unchanged. Production Zig and historical evidence are not edited.

## Real source and exact expansion

Compile unchanged `bench/fixtures/wgsl-corpus/webgpu/sample-prefix-sum.wgsl`,
SHA-256 `a8a642a54aaca908b707d20666566db8db62dbb5c9bfd518fbe98925da22c416`.
Despite its filename it computes adjacent sums, not a full prefix scan. At x=0
it copies input[0] and returns; otherwise it sums input[x-1] and input[x]. This
adds actual statement/control-flow and index-expression ownership, not a
shader-name special case. Source admission is not application adoption.

Retain all milestone-1 positive programs and their semantics. Module/storage
declarations, types, optional single module u32 constant, optional entry-leading
invocation-x alias, names/scopes and workgroup rules are unchanged. Extend only:

- A function body has 1..16 statements in total, including statements nested in
  blocks. The optional leading alias declaration is not counted. Statements are
  a storage assignment, `return;`, or `if (CONDITION) { ... }`, optionally followed
  by `else { ... }`. Empty branch blocks are allowed. At most four nested if
  statements are allowed. Braces and condition parentheses are required;
  `else if`, loops, local declarations inside blocks and bare blocks remain
  unsupported. Parse and validate unreachable statements too.
- A resource index is invocation x (direct or the existing alias), a decimal
  suffixed u32 literal, or the module u32 constant, optionally followed by one
  `+` or `-` and a u32 literal/module constant. No chained or parenthesized index
  arithmetic is admitted. Arithmetic is modulo 2^32. Undefined/shadowed names,
  gid.y/z and f32 indices reject.
- A condition compares an index expression on the left to a u32 literal/module
  constant on the right, with `==`, `!=`, `<`, `<=`, `>` or `>=`. Comparisons are
  unsigned u32 and produce bool. No logical connectives or float comparisons.
- Assignment RHS is a single existing typed operand (resource load, u32 literal
  or module constant), or the existing addition of two such operands. The old
  at-least-one-load restriction is intentionally lifted. Destination and RHS
  scalar types still agree; f32 literals and general scalar expressions remain
  unsupported. Every resource load/store uses its own possibly distinct index.

Statements execute in source order within an invocation. An early return skips
all later statements in that invocation. A branch executes exactly one selected
arm; subsequent code runs only from arms that did not return. Later loads from a
read_write resource observe earlier writes from the same invocation. No ordering
or race-free guarantee is inferred between invocations or aliased descriptors.

## Preserved boundaries and implementation limits

Every computed index is checked against that particular resource's runtime
length. OOB reads produce typed zero and OOB stores are discarded; do not clamp,
wrap to a buffer length, form an OOB pointer before its guard, or jointly gate
unrelated resources. Keep u32 wrap and f32 addition semantics from milestone 1.
Emit SPIR-V 1.3 accepted for Vulkan 1.1 with properly structured selections,
terminators and merges, including nested branches and both-arms-return.

Existing source/token/name/resource/output limits remain authoritative. Statement
and nesting overflow return INPUT_ERROR at the offending original-source token,
with no partial publication. Internal array capacities must be derived from
these limits and checked. No heap/global mutable state, fallback or Zig
dependency is added. Measure the changed static stack frame rather than
pretending the previous footprint still applies.

Keep lexer, parser, semantic/type validation, typed control-flow/value IR,
emitter and API/CLI owners separate. Typed records and small domain-owned helpers
are preferred over a universal AST/visitor/arena framework. Source spelling must
not decide target behavior. The public ABI/header stays byte-identical. Record
explicit grammar-rejection lifts in the new test layer; never silently weaken
old invalid type/scope/access/resource checks or overwrite old receipts.

## Acceptance and bounded effort

1. Preserve all old positives and unchanged residual sources. Compile the exact
   neighbor-sum fixture, identifier/declaration permutations and branch/index
   variants. Check all six comparisons, both branch outcomes, empty arms,
   early/final returns, nested selections, both-arms-return, sequential stores
   and loads, and invalid syntax/type/scope/access/unreachable statements.
2. Independently interpret emitted instructions with hand-authored expected
   buffer results. Include x=0,1,last,first-OOB,UINT32_MAX; empty/unequal buffers;
   underflow/overflow indices; nonzero sentinels; independent source/destination
   indices; y/z nonzero; unchanged inputs/tails. Retain float/u32 residual cases.
   Extend and deliberately fault-test the instruction oracle separately.
3. External Vulkan 1.1 validation covers every admitted fixture. API, reflection,
   wrong ABI/result size, output-capacity atomicity, original diagnostics,
   concurrent calls, guard pages and CLI failures remain tested. Rebuild ordinary
   C/C++ consumers and instrument changed C owners with ASan/UBSan. Report
   LeakSanitizer and physical GPU as not run, not implicit passes.
4. Preserve the previous executable/receipt; record final artifact/source hashes,
   commands, compiler versions, failure corrections and scope limits. Accept
   this compiler increment only after independent review. Production migrated
   owner count remains zero until the broader owner and adopter gates close.

Effort is bounded to this single control-flow/index owner expansion: one
implementation worker and one independent evaluator, the existing private
compiler owners plus focused tests/docs. No new toolchain, runtime, target,
general expression parser, additional local-variable system or numeric literal
grammar is bundled. Widening any of those requires a new declared increment.
