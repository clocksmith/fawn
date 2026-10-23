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

The fixed ABI1/v2 capability owns at most 16 preorder statements and 320 charged scalar
expression nodes. Each if records then/else ranges; the fifth nested if rejects.
Expression depth is at most 18: leaves count one, load(index) adds one, binary/max
adds one to the deepest child, and parentheses add one without another node.
Every storage index and both unsigned comparison operands use this same tree
and typed lowering, not a second atom/index grammar. These are legacy capability limits, not a permanent full-compiler allocation model.
Job capacities use the same owners through checked caller slices.

In the legacy capability, a scalar node emits at most one typed IR operation. Each if adds a comparison
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
values. Preserve legacy alias lookup rules and its no-shadowing capability. Job scopes
allow ordinary shadowing but reject same-scope duplicates; initializer and type
lookup occur before the new binding enters scope. Resolve max and explicit type annotations against
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

## Configurable function jobs and structured state

`storage.c` owns checked count-to-byte multiplication, aligned slice offsets and
ID/header-bound arithmetic. Do not grow fixed arrays with language milestones.
New jobs derive I=1+4*E+8*S instructions, I+1 semantic-value map entries and S+1
local/scope entries; staging words remain independent. Fixed reflection/module
bounds are explicit, not silently expanded by job sizing. Every append/reservation
checks capacity even when a conservative sizing proof exists. The library never
allocates; the caller distinguishes its own allocation failure from compiler
INPUT_ERROR/OUTPUT_TOO_SMALL. Versioned public layout is separate from evolving
language admission; amendments preserve receipts rather than arbitrary bugs.

Keep source scope, semantic mutable-local identity and target pointer identity
separate. Function OpVariable declarations are at entry; initialization stores
are at the original declaration point on every iteration. Immutable bindings
hold evaluated value IDs, not AST expressions to replay. For-header scope
survives test/update/body, but body locals cannot leak into update. A loop body
scope may be referenced by continuing only when a targeted continue cannot
bypass that declaration. Check continuing control restrictions against the
nearest loop and reject return through every enclosing continuing context.

Lower Next/Break/Continue/Return behavior sets bottom-up while still checking
unreachable source. An unreachable continuing block must not invent Next.
The backend suppresses dead operations, not validation, and tracks actual labels
after robust loads and short circuits. A predecessorless loop continuing block
still supplies SPIR-V's structural back-edge, with no source operations emitted.
Each logical RHS has a branch and Phi; eager select/logical operations cannot
stand in for short circuit. Loop labels and target IDs are reserved independently
of semantic values, never by indexing an array with arbitrary target IDs.

Direct integer literals retain AbstractInt until direct contextual conversion.
Do not propagate expected concrete types through unsupported abstract subtrees.
Concrete u32 wraps; floating constant-only trees remain outside this owner.
All name, type, mutability and behavior checks apply to dead source too.

Expression/control depths are independently bounded at 64/32 for jobs, 18/4 for
legacy adapters. Malformed nested continuing syntax also has a checked compound
recursion ceiling of 2*control_depth+1. Neither source/node/statement capacity nor
extra supplied workspace bytes increases stack depth. Record per-function GCC
frames and a conservative combined call-path bound when changing recursion.
The current legacy workspace/frame are 73,400/73,488 bytes; every other library
frame is at most 672 bytes at GCC14.2 -O2. See README for the derived recursion
bounds and exact native/CLI reproduction.
