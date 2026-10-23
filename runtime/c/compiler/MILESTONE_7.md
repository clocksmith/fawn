# C compiler milestone 7: function state, structured iteration and job storage

Frozen before implementation on 2026-10-23. Acceptance is not yet claimed.
Baseline: accepted M6 `6bdc95574aafcc5c4bd8b682bfc09f7feb8c678e`.

## Responsibility and witnesses

Own generic scalar function-local memory and structured control, with explicitly
sized per-call compiler storage. Do not recognize shader names, unroll loops on
the host, assume uniform values or reinterpret test step limits as shader meaning.
The unchanged witnesses are naive matmul, forward/reverse dot product and
forward/reverse matmul logits in the existing corpus. They require var/assignment,
for/loop/break, contextual index `0`, and logical OR; they do not define grammar.

Retained boundaries: `frontend/parser_stmt.zig`; `frontend/sema.zig` scope/local
identity and `sema_typeutils.zig` numeric inference; `ir/ir_builder.zig` typed
local/loop statements; `emit/spirv/emit_spirv_fn.zig` structured control and entry
Function variables. Read-only owner/caller trace and exact witness hashes are in
the external milestone7-planning evidence directory. No Zig code is changed,
invoked or linked by this C work.

## Caller-owned job API

The frozen [job header](include/doe_compiler_job.h) adds only two functions and
one capacities record. It reuses the existing requirements and result-v2 records;
it has no persistent object, allocator callback or result-release function.
ABI1/v2 headers, layouts and exported entrypoints remain byte-identical.

Caller chooses source bytes, tokens, source statements, scalar nodes, output
staging words and separately checked expression/control depths. Requirements are
computed from those limits with checked add/multiply/alignment; caller allocates
once, calls, then frees or reuses. All internal variable-sized arrays are typed
slices of that workspace: tokens, syntax, typed instructions, local scope/identity,
value-to-target IDs and output words. No library malloc/realloc/free, VLA, hidden
second workspace or arrays proportional to untrusted input on the C stack.

Bounds that remain fixed because this increment does not replace their owners:
ASCII identifiers <=63 bytes; eight resources; eight flat structs each with eight
u32/f32 members; five compute inputs; one entry; retained single module u32
constant; binding groups 0..3/bindings 0..31; workgroup dimensions/product 1..256.
Reflection-v2 remains sufficient for exactly that module/resource envelope.
Source spans/diagnostic coordinates remain uint32_t, so source_bytes is at most
UINT32_MAX-1: a final newline or last byte must still have an exact 1-based
coordinate without saturation or overflow.
This is scalable function-job storage, not general module/reflection migration.

For source statement capacity S and scalar-node capacity E, reserve a checked
upper bound I = 1 + 4*E + 8*S typed instructions, I+1 value-map entries and S+1
local/scope records. Each scalar node adds at most four instructions (including
short-circuit boundaries/result), each statement at most eight (loop markers,
updates and generated unit/zero), and one retained alias input is conservative.
Actual appends still check bounds. Target IDs, labels and SPIR-V words have their
own checked reservations; I is not a promise that a program fits the separately
configured word budget. Reserve target IDs conservatively below 256 + 16*I,
with checked uint32 bound arithmetic including the final SPIR-V bound value. Fixed declarations need
less than 256 IDs; each typed instruction may reserve at most 16. No target-ID
indexed array is needed: the I+1 semantic-value map is separate. Every dynamic
ID reservation still checks its limit; instruction/header word counts and
branch labels cannot wrap.

Syntax remains live until lowering ends. Token storage dies after parsing and
may share a correctly aligned phase region with IR/output/scope/value-map storage.
No token pointer survives. Requirements describe the loaded library's private
layout, not a serialized ABI. Extra supplied bytes do not raise configured caps.

The fixed legacy adapters use typed local/caller backing and the same slice-based
pipeline. Their M6 capacities and capability admission remain, including exact
leading-alias accounting and no uniform admission in ABI1. No duplicate parser,
validator or emitter. Legacy scalar-node/depth accounting excludes the outer
comparison node of each old if, just as M6 did. Reserve at most 16 extra physical
syntax nodes for these wrappers; their typed comparisons were already charged
to M6's 385-instruction bound. Their outer comparison adds no legacy expression
depth charge. Required brace bodies remain uncharged; the exact leading alias
stays exempt. Prove old 320-node/18-depth/16-statement/4-if cases without reducing
any admitted bound. Target: legacy queried workspace <=77,760 bytes and ABI1
GCC14.2 -O2 frame <=77,824; compact names/token spans if necessary rather than
increasing fixed arrays. Every library frame except the ABI1 adapter remains
<=8,192 bytes, including recursive parser/lowerer/emitter functions.
Report actual sizeof/alignment/frame measurements, not only planned bounds.

Job expression depth may be configured 1..64, control depth 1..32. These maxima are
independent of node/statement capacities. Expression depth counts all child edges,
including indices, comparisons, logical operations and parentheses. Control depth
counts every nested if/else-if, loop and standalone compound block; a construct's
required braces do not add another level. Continuing belongs to its loop depth;
nested constructs inside it do add depth. Bound parser/lowerer/emitter recursion
by these two envelopes and report the combined worst nesting path. No recursive
walk may follow statement count or sibling count. Legacy 18/4 envelopes remain.

## Publication, failures and CLI

The header owns exact null/version/size/alignment/disjointness and lifetime rules.
ABI/exact-size failure touches no caller storage. Invalid config or workspace
fails before workspace access. Every admitted failure zeroes count/reflection,
reports a bounded original-source diagnostic and preserves every output word.
Successful publication is the only word/reflection write after fallible work.
No source/workspace/output pointer survives the call; reuse needs no reset.

Configuration/size overflow and source/token/node/statement/depth exhaustion use
INPUT_ERROR with a named budget, not OOM. Word staging/caller output exhaustion
uses OUTPUT_TOO_SMALL. Unsupported grammar/types use UNSUPPORTED where recognized;
malformed admitted grammar uses PARSE_ERROR; invalid type/scope/mutability/control
placement uses SEMANTIC_ERROR. Derived IR/target-ID exhaustion is INPUT_ERROR and
must not masquerade as valid output. No library allocator exists, so no library
OOM result is claimed. CLI allocation failures are separate explicit allocation
errors, with all prior allocations/descriptors/temp files cleaned up.

CLI integration: binary-only and reflection-v2 use the job API with
source 65536, tokens 8192, statements 1024, expressions 4096, staging words 65536,
expression depth 32 and control depth 16. This intentionally lifts only those CLI
capacity/grammar rejections named in the new evidence. Legacy --reflection keeps
ABI1 admission. Existing arguments, JSON bytes/schemas, explicit optional sidecar,
no-clobber and ordinary rollback remain. The unchanged --shader-path/--out
consumer needs no new option. Same previously admitted source has identical
current words across the modes. Native API1/v2 limits and failures stay unchanged.
Keep the retained 65537-byte source read buffer and 65536-byte CLI admission;
move enlarged word/serialization buffers to explicitly owned allocations with
checked word-to-byte products. Never enlarge fixed stack arrays. Inject failure at every new acquisition.

## Selected source grammar

Only the new job capability admits this family; old APIs remain exercised bounded
capability adapters. Module declarations, scalar resource layout and compute
interface are otherwise M6. General scalar expressions remain one typed tree.

- `let name [: T] = expr;`, `var [<function>] name [: T] [= expr];`, T=u32/f32/bool.
  At least annotation or initializer is required for var; let always needs one.
  Uninitialized annotated vars receive typed zero/false at declaration execution.
  Every declaration executes again on each dynamic loop iteration. Inference
  follows the numeric boundary below, not a default-u32 shortcut.
- Assignment to a visible mutable scalar local or writable storage element;
  `=`, `+=`, `-=`, `*=`; u32 `++`/`--` statements. No assignment-expression values.
  Compound updates evaluate target/index once, read once, then evaluate RHS,
  compute and write once. Simple assignment evaluates target/index before RHS.
  Storage compound accesses retain independent robust load and store guards.
- Empty statements and standalone brace blocks, return-without-value, if with
  bool expression, optional parentheses, optional else block or else-if chain.
- `for ([initializer]; [bool test]; [update]) { body }`. Initializer is a let/var
  declaration or admitted assignment/update, update is assignment/++/--, and
  omitted test is true. Initializer executes once; test precedes each iteration;
  update follows each completed body or targeted continue. Break/return skip it.
- `while bool-test { body }` with optional parentheses; `loop { body [continuing
  { statements [break if bool-test;] }] }`. Tests and continuing execute at their
  source dynamic frequency, without compiler trip-count assumptions.
- `break;` and `continue;` target the nearest loop. Break outside loops rejects;
  continue outside loops rejects. `break if` is only the final direct statement
  of a continuing block. At most one continuing block, last directly in a loop.
  Continuing rejects return at every nesting level. Break/continue may not exit
  or retarget the loop whose continuing is executing, but control of a nested
  loop inside continuing is allowed. For/while cannot contain a direct continuing.

Source statement accounting counts declaration, assignment/update, return, if,
for, while, loop, continuing, break, continue, break-if, empty statement and
standalone block once each. For initializer/update each count once; required body
braces and the function body do not. An else-if adds another if. Generated
control markers and zero/unit literals consume derived IR slots, not source
statement/node budget. A zero output-word capacity must reject before writing
even the five-word target header. The new job has no leading-alias accounting exception.

Do not add switch, discard, local const, helpers/calls other than max, casts,
general vectors, pointers, aggregates, workgroup memory, barriers, subgroups,
textures, division/remainder/bit operations, numeric unary minus or other targets.
These remain explicit roadmap owners rather than admitted no-ops.

## Scope, state and control meaning

Each declaration has an identity, type, mutability and source scope. A let stores
an evaluated snapshot, not an expression recipe; reading a var loads its current
value. Never substitute a mutable initializer or cache a value across its store.
Function OpVariable records are emitted at entry, but initialization stores stay
at the source declaration point, including declarations inside loops.

The new capability permits ordinary inner-scope shadowing, including a local that
shadows an outer local/resource/parameter. Same-scope duplicates fail; lookup uses
the nearest visible identity. Initializers and annotation names resolve before
introducing the new name. Lookup of max/type names respects shadowing. The entry
body and its parameters share one scope for duplicate-declaration checking.
Disjoint sibling blocks and separate for loops may reuse names. No body/branch
name leaks after its block. Legacy alias/new-let scope behavior stays frozen only
in its bounded adapter capability; no semantic expression duplication.

For initialization owns a scope spanning test/body/update; its body has a child
scope, so body declarations never appear in update and init names expire after
the loop. Loop-body declarations may be visible to its continuing. Reject any
continue that bypasses a declaration referenced from that continuing, including
references in its nested expressions/statements. This is declaration dominance,
not a guess from initializer value or an optimizer constant-condition proof.

Validate all source, including statements after return/break/continue and untaken
logical operands. Emit no executable instruction after a block terminator.
Structured selections/loops retain valid merge/continue targets; predecessorless
required blocks terminate with OpUnreachable. A terminated nested construct does
not invent a fallthrough edge. Validate the small bottom-up WGSL behavior set
(Next, Break, Continue, Return) for every statement, including dead source.
Sequence forwards the second behavior only through Next; if unions its arms
(with Next for omitted else). A loop consumes Continue/Next through continuing,
propagates Return and maps exiting Break to Next. Reject empty resulting behavior.
For/while follow their syntactic desugaring, without constant-condition proofs.
Thus `loop {}` and `loop {continue;}` reject SEMANTIC_ERROR, while
`loop {if false {break;}}` and `while true {}` may compile and run indefinitely.
The latter programs remain unfinished at a harness limit, never silently finite.
Use [WGSL statement behavior rules](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#statement-behavior-rules),
not an optimization inference, to distinguish these cases.

Use actual SPIR-V Function memory, OpLoad/OpStore, OpLoopMerge and branches.
Immutable values must dominate uses, including repeated loop declarations and
continuing references. Track actual current labels after nested robust accesses
and short-circuit merges for every Phi predecessor. Preserve per-access OOB-load
zero/OOB-store discard, wrap-before-check, and uniform minimum binding preconditions.

## Boolean and numerical boundary

Admit true/false, bool let/var, `!`, `&&`, `||`; comparisons ==/!= on bool and
u32/f32, and ordered <,<=,>,>= on u32/f32. Both numeric operands must select a
compatible type; bool is not numeric and there is no truthiness. Conditions need
bool. Same-operator logical chains associate left; mixed unparenthesized &&/||
and chained relational comparisons reject per WGSL, not C precedence.
Short circuit branches around RHS evaluation; a select or eager OpLogicalAnd/Or
is not a substitute. Even skipped RHS source must pass type/name validation.
F32 comparison instructions must implement their actual relation (including
unordered not-equal), without broadening the existing finite numerical/GPU claim.

Retain the exact bounded decimal-rational to f32 conversion and runtime operation
order. Add unsuffixed decimal integer literals, no leading zero except 0, with
checked uint32 magnitude. Their semantic kind is AbstractInt, not u32. A direct
literal, including parentheses, may materialize to a required u32 or f32. For a
numeric operator/comparison/max with a concrete u32 operand (constant or runtime),
a direct abstract integer sibling may materialize to u32: `1u+1`, `max(1,2u)`
and `1<2u` admit. For f32, that materialization applies only where the retained
constant-only floating-expression exclusion does not apply. Explicit u32 never
implicitly becomes f32.
A standalone inferred unsuffixed integer let/var defaults to i32 in WGSL and is
UNSUPPORTED here, not silently inferred u32. A direct unsuffixed storage index
must be <=INT32_MAX (the preferred i32 materialization); after proving it
nonnegative, equivalent u32 index encoding is allowed without claiming signed
runtime values. A suffixed u32 index retains its full UINT32 range.

Abstract-only arithmetic/max subtrees are explicitly UNSUPPORTED, even when an
outer concrete type would later consume the result. Do not propagate an expected
u32 type inward and wrap a subtree such as (1-2). This increment does not own an
AbstractInt/AbstractFloat evaluator. Comparisons of two direct abstract integer
literals may compare their exact magnitudes and produce bool; no concrete type
or runtime local is invented. Constant-only f32 arithmetic/max remains unsupported,
including explicitly suffixed forms, as before. Likewise, constant-only float
comparisons (including explicitly suffixed or mixed abstract-int/float operands)
are UNSUPPORTED rather than comparing prematurely rounded abstract operands; a
runtime f32 operand makes its direct literal sibling materializable. A let or var
read is runtime stage even when initialized by a literal. These limits also apply in dead source.

Required paired numeric cases: concrete `0u-1u` and `4294967295u+1u` retain modulo
u32 semantics, as do runtime-local operations. `(1-2)+1u` and `(1+2)+1u` are both
explicitly unsupported abstract subtrees. `var x:u32=1`, `var x:f32=1`, `out[0]`
and `runtime_u32+1` admit; inferred `var x=1` and `out[2147483648]` do not. Direct
`4294967296` is out of this checked literal range and cannot silently truncate.

An initial planning suspicion about concrete constant overflow was disproved:
WGSL defines modulo concrete integer arithmetic also for constant expressions.
No compatibility semantic fork or old-test rewrite is justified. See the dated
[WGSL integer and abstract type rules](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#integer-types)
and [control statements](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#control-flow).

## Independent acceptance to freeze before implementation

- All five unchanged witnesses through job API, binary/reflection-v2 CLI and
  unchanged emitter consumer; explicit Vulkan1.1 target validation. Raw separately
  packed resources and emitted-opcode execution establish scalar/loop results.
  Keep the same 98-path ledger and distinct parser/sema/emission/target/execution
  stages. No GPU/runtime qualification from CPU interpretation.
- Generic mutable snapshots, zero initialization on every iteration, repeated
  local reads/stores, bool locals, nested/sibling shadowing, duplicates, self/use
  before declaration, annotation/builtin shadowing and unchanged invalid types.
- Zero/one/many iterations; omitted for clauses; test/body/update ordering;
  nearest nested break/continue; return bypass; both-arm abrupt termination;
  continuing/break-if validity and bypassed-declaration rejection; long real
  dynamic loops. Static-empty-behavior loops reject, including dead occurrences;
  syntactically admissible dynamic-infinite cases hit a harness-only step bound
  as unfinished, not compiler-inserted termination or a correctness pass.
- Actual short-circuit RHS nonexecution and post-guard Phi predecessors; nested
  conditions, expressions and loop tests containing independent robust loads.
  Unequal/empty logical storage, source-short/output-long, wrap-to-valid and OOB
  inner-load-zero selecting a valid outer access. Preserve read-only inputs.
- Every public preflight and exact limit, arithmetic/alignment query overflow,
  undersized/misaligned workspace and short/staging word buffers; redzones,
  repeated success/failure/reuse, distinct concurrent jobs, and stale-config size
  mismatch. Demonstrate jobs above legacy source/token/node/statement/word limits
  without growing legacy arrays. Caller/CLI allocation-failure cleanup remains
  separately exercised; no allocator fault is falsely called a library OOM.
- Retained M1-M6 semantics, native C/C++ source/prebuilt consumption, ABI layouts,
  exports/interposition, CLI file failure/rollback and sanitizers. Old rejection
  lifts are capability-tagged, not edits to frozen suites/receipts. Opcode mutation
  controls catch wrong loop order, eager RHS, stale local, missing zero init,
  wrong target/merge/predecessor and missing robust guards.
- Source/contract/artifact hashes, actual typed layout/stack and build/check
  observations. No performance improvement claim without matched measurement.

Independent contract review passed before implementation. This contract authorizes
only the isolated compiler increment and its evaluation under the migration plan.
It does not authorize edits to historical receipts, production selection or public
publication. Acceptance and local implementation commits require source-bound tests.
