# C compiler implementation style

Follow the repository's native adapter rules and the frozen milestone. This local guide records implementation choices, not expanded language or qualification claims.

- Use C11, explicit status/boolean results and caller-owned spans. Check lengths and capacities before indexing, casts and publication.
- Keep lexical, parsed, validated and target representations separate. Emission consumes typed binding identities and operations, never source spelling.
- Preserve source offsets through semantic lowering. Report the first admitted pipeline failure with a useful bounded message.
- Keep ownership visible: typed caller workspace (v2) or private stack workspace (ABI1), explicit CLI allocations and file descriptors with one cleanup path, no retained caller pointers or mutable globals. Optional reflection is a nullable path, not a default filename; only a successfully acquired temporary or published artifact is eligible for cleanup.
- Prefer typed, bounded records over generic containers, callback frameworks or erased `void *` domain values.
- Use named small helpers for parsing, resolution, validation and emission. The emitter's local serialization macro only supplies an array length and propagates failure; it is not an IR or dispatch framework.
- Keep test evidence separate from implementation. Test semantics, error/output atomicity and real native consumers, not desired source snippets. Keep first failures when repairing implementation or classifying intentional contract expansion.
- Preserve MIT notices and imported-source identity. Do not mechanically translate production Zig or mutate historical experiments.

## Structured owner limits

The private syntax owns at most 16 preorder statements and 320 unified scalar
expression nodes. Each if records then/else ranges; the fifth nested if rejects.
Expression depth is at most 18: leaves count one, load(index) adds one, binary/max
adds one to the deepest child, and parentheses add one without another node.
Every storage index and both unsigned comparison operands use this same tree
and typed lowering, not a second atom/index grammar. These are this increment's
explicit envelope, not a permanent full-compiler allocation model.

A scalar node emits at most one typed IR operation. Each if adds a comparison
and three control markers; other statements add at most one operation. Only an
exact leading unannotated global-invocation-x alias adds one exempt input value,
with no statement or node charge. All other builtin reads count as nodes, with
no input prelude. Thus 1 + 320 + 16 * 4 = 385 instructions and 386 value-map slots
remain sufficient. Reusing locals adds no operations. Appends and serialization
check their own bounds, including the separate 4,096-word output cap.

Compute input declarations own semantic identities independently of order/name.
The four vector builtins have u32 x/y/z components; local_invocation_index is
scalar. The emitter maps those identities to BuiltIn decorations and includes
all and only declared input variables, including unused inputs. Entry-point
argument storage derives from the maximum name length and five inputs. Zero
resources, parameters or body statements never fabricate a global input.
Workgroup dimensions and their checked product remain within the frozen bound.

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
predecessors without ever adding a second terminator after return. Keep the ABI1 public
header byte-identical; v2 has its own frozen header, and private workspace is measured, not hidden.

## Resource layout and workspace ownership

Represent module resources and flat struct/member declarations independently.
Type resolution is declaration-order independent; member identity is an owner
struct index plus member index, never a resource/field spelling convention.
Validate the full module namespace before lowering. Use the same typed resource
and layout IR for emission and each reflection schema. Uniform access uses the
Uniform storage class with Block/Offset and fixed member indices, not runtime
array length or short-buffer repair. Storage robustness keeps each access's own
binding, even beside uniform loads and early returns.

Syntax owns copied declaration names and compact offset/length spans for scalar
expression names. Validate spans against source length before use; no expression
stores a 64-byte name or token pointer. IR/reflection own published names. Syntax
survives parsing while the caller source remains borrowed for the complete call. TokenList lifetime ends at
parse return, permitting its union storage to become the combined typed program
and target scratch. Do not retain token pointers or reinterpret live token state
as IR. ABI1 has a real typed local workspace, while v2 accepts correctly aligned
malloc/aligned-allocation storage after version/size/alignment preflight. One
shared capability-parameterized pipeline performs lexical admission through
emission. Result layout projection stays at its API wrapper's successful
publication point. No library allocator or implicit second workspace is allowed.

Every nested robust load finishes its own merge before the surrounding index
arithmetic or comparison uses its value. OOB loads yield typed zero, not an
invalid flag; zero may select a valid outer read or store. Wrap u32 arithmetic
before independently guarding the final index of each binding. Keep uniform
minimum binding requirements separate from storage robustness. CLI JSON owns
bounded append mechanics, not another semantic/reflection registry.
