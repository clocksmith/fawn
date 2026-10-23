# C compiler milestone 10: typed aggregates, access paths and reflection

Frozen after lead and independent review, 2026-10-23. Not implemented or
accepted. The shared compiler owner, output contract, safety limits and
[independent obligations](MILESTONE_10_EVALUATION.md) govern implementation.
Concrete evaluator cases must be sealed before candidate inspection/execution.

## 1. Decision and source identity

One general compiler owner replaces scalar-shaped syntax/IR/reflection seams:
constructible vectors, fixed arrays and nominal structures; typed values and
references; compact constants; checked layouts; and one additive native output
projection. All APIs use that owner. No recognizer for a shader, filename,
resource name, witness dimensions or corpus spelling is permitted.

Retained accepted M8: `4e03682849db8101cc8ed35a8243433c305852d5`.
Current accepted M9 baseline: `dee5f42acbb50e05d5690f1d2d13777c5c26d51d`,
verified checkout HEAD on branch `experimental/c-migration-dot-20261023`.
Its contract was frozen at `1aa59cd5fe3987a85e8d6348bad4a96326a5cb3c`.
Read `runtime/c/compiler/MILESTONE_9_RESULTS.md`: independent acceptance records
380 new plus 997 M8 plus 839 older cases, and 33/98 actual consumer paths.
These are accepted M9 evidence, not M10 results. Acceptance completed while this
planning pass was in progress; only baseline/status/hash metadata was rebound.

The prior source-bound trace is
`../../next-owner/aggregate-types/trace.md`. Its 170-file `SOURCE.sha256`
manifest was read and all 170 files re-hashed equal before M9 acceptance.
The accepted commit changes four of those files, all navigation/provenance:
COMPILER_ROADMAP.md, MIGRATION.md, compiler/PROVENANCE.md and compiler/README.md.
No traced implementation/header/witness changed. This bundle's SOURCE.sha256
records the current 170 identities plus MILESTONE_9_RESULTS.md and the M9 corpus
ledger (172 paths); the original 170-file manifest remains untouched at its
trace owner. PLANNING.sha256 seals these planning files. No compiler implementation, candidate test, production build, download, deletion
or Git mutation was performed by this pass. The sole execution exception is the
authorized standalone GCC record-layout probe in this planning bundle; its
source, command, sizes and frame receipt are retained in MILESTONE_10_LEDGER.md and companion
files. DRAFT_V1.md preserves the prior revision unchanged.

Preserve production/default Zig, every historical contract/result/source, the
MIT/license and seed provenance, existing five exports and three header bytes.
No public push, fleet/user-computer/Gamma/private-trace action, runtime adoption,
physical GPU evidence, speed claim or full migration claim follows from this.

### Decisive choices

- Add `doe_compiler_typed.h`, ABI4, exactly two functions: workspace requirements
  and synchronous compile with optional measure-only output. No prepared handle,
  initializer/destructor, callback allocator or generic arena API.
- Caller owns typed API workspace and output spans. No library heap fallback.
  All declared structures/resources are reflected, including unused declarations.
  Named structures retain nominal identity; anonymous shape is not a substitute.
- Legacy adapters reject an unrepresentable projection before publishing words.
  They do not parse a smaller language or silently omit a new structure. Local
  vectors/arrays and aggregate helper values with representable reflection work
  in old modes when actual capacities fit.
- One natural-layout result feeds validation, typed reflection and SPIR-V.
  Uniform constraints validate that result; they never silently repack it.
- Explicit pools, checked actual appends and a target lowering ledger replace
  assumptions that token count bounds flattened aggregate bytes. Old stack
  ceilings are reviewed safety envelopes, not permanent architecture.

## 2. Exact semantic boundary

Retain every accepted scalar/control/function rule and historical accounting
exception. Admit the following family independently of resource projection:

1. `vec2/vec3/vec4` of bool/i32/u32/f32, plus the standard `vecNu`, `vecNi`,
   `vecNf` predeclared spellings. Abstract numeric vectors exist only at const
   stage where inference allows them. No f16 spelling is admitted. Vector type
   constructors use ordinary name lookup, including local/module shadowing.
2. Constant-length arrays of admitted constructible elements, with positive
   const-expression counts resolving to i32/u32. Count value, not spelling,
   establishes identity. Explicit array constructors, zero constructors and
   inferred `array(...)` construction are included. No override-sized arrays.
3. Module-scope named, nonempty structures with ordered, uniquely named members
   composed from the family; forward references and nesting; zero and complete
   positional construction. All structure member types are concrete. Distinct
   declarations are never equal merely because their fields have equal shapes.
4. Explicit and inferred local let/const/var aggregate values, zero-initialized
   vars, full copies, by-value immutable helper parameters and by-value results.
   A result/formal may be any constructible admitted type; void remains absence
   of a result annotation. No source pointers, reference parameters or capture.
5. Arbitrary postfix member/index chains on both values and references, including
   `buffer[i].member[j].x`, constructor/call results, and grouped expressions.
   Vector reads accept one alphabet per swizzle (`xyzw` or `rgba`), length 1..4,
   with repetition allowed and every component in range. Single-component
   writes are included. Multi-component swizzle writes remain explicitly out.
6. Existing scalar operator families lift only to WGSL-defined vector overloads:
   component arithmetic, unary negation/complement/not, integer bit operations
   and shifts, and component comparisons. Numeric scalar/vector combinations
   are allowed only for operators with such overloads, not general broadcasting.
   Equality/comparison of vectors produces a bool vector; a control condition
   still requires a scalar bool. No array/structure arithmetic or equality.
   Runtime division, shifts and conversions guard each component using the same
   scalar semantics. Constructors cover zero, scalar splat, exactly filling
   scalar/vector components, same-type copies and allowed component conversion.
   Vector bitcast is included only for equal-lane i32/u32/f32 vectors; this
   adds no f16 packed or shape-changing bitcast family.
7. M9 math builtins remain scalar in this increment. Vector min/max/clamp,
   exp/tanh/sqrt and new dot/floor/reduction/transcendental operations are not
   smuggled into the type owner. The three fluids witnesses require ordinary
   vector operators/constructors but their clamp calls remain scalar.
8. Storage/uniform roots may be admitted host-shareable scalars, vectors, fixed
   arrays or structures. Storage additionally permits a runtime array directly
   or as the direct last member of a root structure. Its element must have
   creation-fixed footprint. A structure with a runtime tail cannot itself be
   a member/array element, local value, constant, argument or result. Uniform
   cannot contain runtime arrays or bool at any depth. Host-shareability is a
   separate property, not the definition of a usable local type.
9. Member `@align` and `@size` are included, resolved with the ordinary constant
   owner, validated even on unused declarations, and preserved explicitly in
   reflection. No ignored attributes. Other unsupported attributes keep their
   typed rejection. Named structs may be unused or local-only in use without
   being omitted from reflection.

Continue the current single compute entry/input/workgroup envelope and
SPIR-V 1.3 / Vulkan 1.1 environment. This proposal retains current group/binding
coordinate bounds and identifier spelling/63-byte bound; it removes the new
API's fixed eight-resource/eight-structure/eight-member output counts. Actual
caller pools bound counts. More resources does not claim a device limit change.

Separate owners remain matrices, source pointers/address-of/dereference,
`arrayLength`, aliases, overrides, workgroup/private globals, zero-init/shared
workgroup lifetime, barriers/uniformity, atomics, extensions, multi-entry,
graphics, textures, other targets and native adoption. Internal typed references
and Function temporary storage do not introduce a source pointer API. Runtime
array guarding does not require exposing source `arrayLength`. Retained tests
using it are dependency witnesses, not mandatory unchanged M10 positives.

### Exact operator and constructor table

These are the only new overloads. `S` is one scalar type, `Vn<S>` a vector of
width n=2,3,4; `N` is numeric (AbstractInt/AbstractFloat/i32/u32/f32), `I` integer
(AbstractInt/i32/u32), and `B` bool. Abstract types occur only at const stage.
Resolve the existing common-type/abstract materialization rules before selecting
a row; no implicit conversion between distinct concrete numeric types is added.
Unless stated, both operands have the same element type and shape. `shape(B)`
means scalar bool or the same-width bool vector. Control conditions require B.

| Operation | Admitted operand forms | Result |
|---|---|---|
| unary - | N or Vn<N>, excluding u32 | same type |
| unary ~ | I or Vn<I> | same type |
| unary ! | B or Vn<B> | same type |
| +, -, *, /, % | N,N; Vn<N>,Vn<N>; N,Vn<N>; Vn<N>,N | scalar for first; Vn<N> otherwise |
| &, \|, ^ | I,I or Vn<I>,Vn<I> | same integer shape |
| eager &, \| | B,B or Vn<B>,Vn<B> | same bool shape |
| &&, \|\| | B,B only, short-circuit | B |
| ==, != | S,S or Vn<S>,Vn<S>, including bool | shape(B) |
| <, <=, >, >= | N,N or Vn<N>,Vn<N> | shape(B) |
| <<, >> | I,u32 or Vn<I>,Vn<u32> | LHS type |
| bitcast<Vn<T>> | Vn<U>, T/U each i32/u32/f32, same n | Vn<T> |
| ++, -- | writable scalar i32/u32 reference only | statement, no value |

The scalar/vector arithmetic rows splat the scalar for that operation. There is
no such mixed-shape overload for comparison, equality, bitwise operations or
shifts. `^` has no bool row. All bitcast, shift-count, const exceptional-value
and per-lane runtime safety rules retain M8/M9 scalar semantics. A compound
`op=` is admitted exactly when its binary `op` row accepts LHS and RHS and the
result has the LHS store type; it follows the reference/read/RHS/write order in
section3. No array/struct arithmetic, equality or arbitrary operator lifting.

Constructor table (`T` in explicit type syntax is an admitted concrete scalar;
`E` is constructible; `SName` is one nominal declared struct):

| Spelling/form | Exact rule |
|---|---|
| vecn<T>() / vecn() | typed zero / Vn<AbstractInt> zero |
| vecn<T>(scalar) / vecn(scalar) | splat one scalar of T after allowed abstract materialization / infer scalar type |
| vecn<T>(Vn<U>) | same-width component conversion using the existing scalar constructor conversion U->T |
| vecn(Vn<U>) | same-width copy, infer U |
| vecn<T>(components...) | scalar/vector pieces exactly fill n lanes; common T after abstract materialization |
| vecn(components...) | same lane rule; infer one common scalar element type |
| array<E,N>() | zero of explicit positive constant length N |
| array<E,N>(values...) | exactly N elements, each E after abstract materialization |
| array(values...) | nonempty; infer one common element type, count is argument count |
| SName() | zero of that nominal type |
| SName(values...) | exactly member_count positional values matching each declared member type |

`vecn` above means vec2, vec3 or vec4, not a source spelling; vecNu/i/f aliases
use the identical explicit-type rules. The complete multi-argument vector lane
partitions are n2:1+1; n3:1+1+1,2+1,1+2; n4:1+1+1+1,2+1+1,1+2+1,1+1+2,2+2,
3+1,1+3. A sole scalar is the splat row; a sole same-width vector is the conversion/
copy row. Component-list rows do not implicitly convert concrete f32/i32/u32
mixtures. No scalar conversion is smuggled into a splat: explicitly convert the
scalar first when its concrete type differs. Existing scalar constructors stay.

No zero-argument inferred array, runtime-array constructor, array copy
constructor, struct copy constructor, named-field constructor, default member
value or partial initialization is added. Whole-value assignment and by-value
calls still copy arrays/structs. Thus SName(existing_same_struct) is not a copy
constructor (unless its one member actually expects that value's type, which a
valid acyclic same-type struct cannot do). All forms obey ordinary scope lookup,
const-stage checking and nominal identity, including unused declarations.

## 3. One semantic, constant and access representation

### Logical identity and scope

Source annotations keep compact source-linked TypeRefs; resolved expression
results, locals, formals/results, resource store types and IR values carry checked
logical TypeIds. Primitive IDs need no pool record; repeated scalar/nominal
annotations need no new shape record. Resolve original spelling through scope
before mapping a primitive. Exact records and pool accounting are in MILESTONE_10_LEDGER.md.
The scalar-kind enum remains a property of a scalar leaf, never a packed vector
code. Types are a closed tagged set: void, scalar, vector, fixed array, runtime
array, nominal struct. Source reference/value categories are distinct records;
a resource or mutable local yields a reference carrying its real store type.

Hoist module names and nominal struct IDs before resolving fields/signatures.
Lookup remains nearest local scope, own immutable formals, module, predeclared
names. A binding enters after its initializer; members belong to their type,
not the module namespace. Helper return/formal annotations resolve in module
scope, not against their formal names. Check all dead/unused source and all
signature/field/extent/attribute dependencies.

Use one iterative declaration-dependency resolver for const references, type
extent/attribute dependencies and struct-field edges. Detect mixed cycles too
(e.g. an extent depends on constructing the containing type), not just direct
`struct A { a:A }`. Node colors and explicit cursor/parent work items live in
caller pools. Function call-cycle validation remains iterative and separate.
No C recursion proportional to a chain of declarations or nested type identity.
Syntactically nested type parsing uses the declared type-syntax depth budget.

### Constants and materialization

Keep NumericValue and the accepted numeric environment for scalar leaves, stored
inline as before. Aggregate ConstIds own ZERO(type), REPEAT(type, value, count),
or COMPOSITE(type, child-span) nodes. Child ConstValues contain either inline
scalar bits or an aggregate ConstId, never copied aggregate payloads. Scalar-only
old inputs consume no aggregate nodes/edges. Copies retain immutable DAG
identities; zero projection never expands its count. REPEAT is limited to vector
splats (count<=4), not a new array-splat constructor or hidden array expansion. No raw C structure bytes or
host ABI packing represents a WGSL constant.

Each aggregate result carries TypeId, const/runtime stage and value/reference
category. Abstract inference is component-aware; resolve overload first, then
materialize only where required. Converting a concrete aggregate is not abstract
materialization. Struct construction materializes each component to its declared
field type; arrays require one common element type, not per-element runtime
variants. Runtime indexing concretizes an abstract aggregate before extracting.
Let bindings remain runtime snapshots even when initialized from constants.

A materialization work item is keyed by (ConstId, destination TypeId). Memoize
committed conversions; do not intern losing speculative types or recursively
clone constant DAGs. Explicit constant child lists charge actual edges. Zero
initialization and array length never allocate one scalar slot per element.
If a target operation truly needs expanded operands, first check actual target
declaration, instruction-width and word budgets; report the corresponding capacity
or target-format failure, not fictitious constant-expression invalidity.

### Values, references, copies and order

An access path owns root identity, address space/access mode, root TypeId and
an ordered range of MEMBER, INDEX and vector-component steps. Each step records
input/output TypeId and original source offset; dynamic index steps reference
one already evaluated integer ValueId. A value base owns its snapshot; a
reference base owns no pointer to an AST to replay. Nonassignability propagates
through let, const, formal and read-only resource bases.

Evaluate call/constructor arguments and ordinary eager operands left-to-right,
once. Scalar &&/|| retain short-circuit evaluation. Simple assignment evaluates
the LHS reference, including its root and index expressions, then RHS, then the
guarded write. Compound assignment evaluates the LHS reference once, reads and
snapshots its old value, evaluates RHS once, applies the corresponding binary
operation to old value and RHS, then writes through the captured reference.
The old-value load is BEFORE RHS evaluation (dated WGSL 9.2.4), preserving M9.
An OOB old-value read produces typed zero; an OOB final write is discarded,
but required LHS/RHS effects still occur. Increment/decrement use the same
reference/read/write sequence without a source RHS; scalar i32/u32 only.
Do not skip a later index-expression call because an earlier reference step is
OOB; skip only invalid pointer construction/load/store. Whole-value assignment
reads the source before replacing the destination. By-value call arguments and
results cannot alias mutable caller locals or be overwritten by later arguments.

Every helper call remains conservatively storage-writing. No cached load crosses
it. No restrict/no-alias assumption across bindings. Copying a field or element
updates only that source-language object; untouched sibling values survive.
Padding bytes are not WGSL value components; qualification compares meaningful
bytes and guards outside ranges, not an invented padding-value promise.

## 4. Layout and robust-access owner

Normative reference points: [WGSL CRD 2026-10-21](https://www.w3.org/TR/2026/CRD-WGSL-20261021/),
sections 6.2, 9.2, 12.1/12.14, 13.3.4 and 14.4. In brief: vec3 of 32-bit
components has size12/alignment16; array stride rounds element size to element
alignment; runtime arrays occur only at a storage root or its direct last
member; conservative uniforms impose additional alignment/spacing constraints.
The rules below are this owner's explicit contract, not a claim that retained
Zig's separate natural/uniform layout implementations agree.

Compute a `TypeLayout` once per logical type, iteratively after dependencies:
fixed-size-known, natural alignment, fixed size (if known), array stride, member
offsets/effective sizes/effective alignments, host-shareability, constructibility
and runtime-tail identity. Use checked uint64 arithmetic for every add, product
and round-up; check target representability separately before decoration/IDs.
No C sizeof and no `member_index*4` special case.

Proposed concrete rules for the admitted 32-bit family:

- Scalar size/alignment4. Vector width2: 8/8; width3: 12/16; width4: 16/16.
  bool follows local layout bookkeeping but remains non-host-shareable.
- Fixed-array alignment=element alignment, stride=roundUp(alignment,element
  size), size=count*stride. Runtime-array stride uses the same calculation but
  has no fixed size. Zero in a public unknown-size field is tagged, not size0.
- Member effective alignment is explicit @align if present, otherwise its type
  alignment. @align is a positive power of two at least that natural alignment.
  Effective size is @size if present, otherwise the fixed type size; @size is
  positive, no smaller than that size, and forbidden on a runtime-array member.
  Attribute expressions resolve to concrete integer constants; duplicate or
  misplaced attributes fail. Member offsets round the previous member end up
  to the next effective alignment; struct alignment is the maximum effective
  member alignment and fixed struct size rounds the final end to that alignment.
- A runtime-tail root has an exact tail offset/stride and no fixed size. Minimum
  binding extent is the same layout evaluated with one tail element, including
  root struct trailing alignment rounding. Direct runtime-array minimum is one
  stride. Fixed roots use their fixed size. Runtime array length uses the
  effective binding range's `(range - tail_offset) / tail_stride`, with range
  admission/underflow handled before this arithmetic, not the minimum extent.
  This keeps min-binding size, tail offset and dynamic length distinct.
- Conservative uniform policy only: `uniform_buffer_standard_layout` is not
  supported by this increment. RequiredAlignOf for arrays/structures rounds
  natural alignment up to16; scalar/vector requirements remain natural. Check
  each member offset against its member type's required alignment, arrays for
  stride multiple16 and element required alignment, and spacing after nested
  struct members for at least roundUp(16,nested fixed size). Check recursively.
  Do not raise natural offsets/strides to make a failing uniform valid. The
  existing three-scalar uniform remains minimum12, natural alignment4,
  required alignment16. Local-only/std430-valid types are not rejected just
  because uniform use of the same shape would be invalid.

Keep all representable arithmetic separate from the selected target envelope.
Propose a maximum emitted Offset/ArrayStride operand of UINT32_MAX; a larger
required literal is a named target-format UNSUPPORTED result before publication.
64-bit reflection sizes do not promise 64-bit GPU indices or device limits.
Fixed counts must fit a positive u32 and instruction count must fit SPIR-V's
16-bit word-count field. No arbitrary cap derived from source byte count.

Checks with known expected layouts must include:
`struct {v:vec3f,x:f32}` => offsets0/12,size16,align16;
array<vec3f,2> => stride16,size32;
`struct Inner{x:f32}` => size4;
`struct {a:Inner,b:f32}` => storage offsets0/4,size8 but invalid uniform;
`struct {@align(16) @size(16) a:Inner,b:f32}` => offsets0/16,size32;
`struct {h:vec3f,tail:array<f32>}` => tail12,stride4,minimum16;
`struct {@align(32) h:u32,tail:array<vec3f>}` => tail16,stride16,minimum32.
Acceptance must independently recompute these and more varied cases.

### Every access is safe before pointer construction

For a known-length vector/fixed array, reject a constant out-of-range index,
including negative signed indices, during required semantic checking. A runtime
index produces zero on read and discards on write if out of range. This is an
explicit deterministic robustness choice consistent with earlier storage rules.
Runtime-tail indexing is always bounded by that resource's dynamic element count.

Each reference step checks its own bound. A signed index must pass nonnegative
and upper-bound checks before conversion/use. Only the valid branch may form
its OpAccessChain or invoke a dynamic extraction with that index. Never compute
an invalid pointer first and then select its loaded value. A failed earlier step
carries validity=false to later steps while evaluating independent side effects;
no pointer is formed from a nonexistent subobject. An OOB load yields the full
typed zero, including aggregates, not an untyped scalar zero.

A robust resource load used as another resource's index is a separate operation:
its OOB zero may legally index the outer resource. Do not combine unrelated
resource lengths/guards or suppress that valid outer access. Uniform/fixed-root
minimum-range admission, descriptor alignment, range/usage, lifetime and writable
resource overlap are host/runtime preconditions, not compiler-generated zeroing.
Do not infer physical acceptance from logical zero-length interpreter tests.

## 5. Exact additive native header

The following is the complete frozen `include/doe_compiler_typed.h` content.
Existing headers do not change.
Use existing DoeCompilerStatus numbers; new failure detail lives in result fields
and diagnostics, not a silent extension of the old scalar enums.

```c
/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Experimental typed standalone compiler ABI, version 4. */
#ifndef DOE_COMPILER_TYPED_H
#define DOE_COMPILER_TYPED_H
#include "doe_compiler_v2.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_TYPED_ABI_VERSION 4u
#define DOE_COMPILER_TYPED_SCHEMA_VERSION 4u
#define DOE_COMPILER_TYPED_NONE UINT32_MAX
#define DOE_COMPILER_TYPED_MAX_EXPRESSION_DEPTH 64u
#define DOE_COMPILER_TYPED_MAX_CONTROL_DEPTH 32u
#define DOE_COMPILER_TYPED_MAX_TYPE_SYNTAX_DEPTH 64u

#define DOE_COMPILER_TYPED_WORDS 1u
#define DOE_COMPILER_TYPED_REFLECTION 2u
#define DOE_COMPILER_TYPED_MEASURE 4u
#define DOE_COMPILER_TYPED_PROFILE_CONSERVATIVE_UNIFORM 1u

#define DOE_COMPILER_TYPE_SCALAR 1u
#define DOE_COMPILER_TYPE_VECTOR 2u
#define DOE_COMPILER_TYPE_ARRAY 3u
#define DOE_COMPILER_TYPE_STRUCT 4u
#define DOE_COMPILER_TYPE_RUNTIME_ARRAY 5u
#define DOE_COMPILER_LEAF_BOOL 1u
#define DOE_COMPILER_LEAF_I32 2u
#define DOE_COMPILER_LEAF_U32 3u
#define DOE_COMPILER_LEAF_F32 4u
#define DOE_COMPILER_TYPE_CONSTRUCTIBLE 1u
#define DOE_COMPILER_TYPE_HOST_SHAREABLE 2u
#define DOE_COMPILER_TYPE_FIXED_SIZE 4u
#define DOE_COMPILER_TYPE_HAS_RUNTIME_TAIL 8u
#define DOE_COMPILER_MEMBER_EXPLICIT_ALIGN 1u
#define DOE_COMPILER_MEMBER_EXPLICIT_SIZE 2u
#define DOE_COMPILER_RESOURCE_RUNTIME_TAIL 1u

#define DOE_COMPILER_POOL_NONE 0u
#define DOE_COMPILER_POOL_SOURCE 1u
#define DOE_COMPILER_POOL_TOKENS 2u
#define DOE_COMPILER_POOL_STATEMENTS 3u
#define DOE_COMPILER_POOL_EXPRESSIONS 4u
#define DOE_COMPILER_POOL_TYPES 5u
#define DOE_COMPILER_POOL_MEMBERS 6u
#define DOE_COMPILER_POOL_RESOURCES 7u
#define DOE_COMPILER_POOL_ACCESS_STEPS 8u
#define DOE_COMPILER_POOL_CONSTANTS 9u
#define DOE_COMPILER_POOL_CONSTANT_EDGES 10u
#define DOE_COMPILER_POOL_IR 11u
#define DOE_COMPILER_POOL_IR_OPERANDS 12u
#define DOE_COMPILER_POOL_TARGET_DECLARATIONS 13u
#define DOE_COMPILER_POOL_TYPE_SYNTAX 14u
#define DOE_COMPILER_POOL_TRAVERSAL 15u
#define DOE_COMPILER_POOL_WORDS 16u
#define DOE_COMPILER_POOL_EXPRESSION_DEPTH 17u
#define DOE_COMPILER_POOL_CONTROL_DEPTH 18u
#define DOE_COMPILER_POOL_TYPE_SYNTAX_DEPTH 19u

/* Limits name logical pools, not private record sizes or stable byte offsets. */
typedef struct {
    size_t source_bytes, tokens, statements, expressions;
    size_t types, members, resources, access_steps;
    size_t constants, constant_edges, ir_instructions, ir_operands;
    size_t target_declarations, traversal_items, words;
    uint32_t expression_depth, control_depth, type_syntax_depth, reserved;
} DoeCompilerTypedCapacities;

typedef struct {
    uint32_t offset, length; /* In the names byte span; excludes trailing NUL. */
} DoeCompilerNameV4;

typedef struct {
    uint32_t kind, flags;
    DoeCompilerNameV4 name;
    uint32_t scalar_kind, element_type, vector_width, array_count;
    uint32_t first_member, member_count;
    uint64_t size_bytes, alignment_bytes, stride_bytes;
} DoeCompilerTypeV4;

typedef struct {
    DoeCompilerNameV4 name;
    uint32_t type_index, flags;
    uint64_t offset_bytes, size_bytes, alignment_bytes;
    uint64_t explicit_align_bytes, explicit_size_bytes;
} DoeCompilerMemberV4;

typedef struct {
    DoeCompilerNameV4 name;
    uint32_t group, binding, kind, access, type_index, flags;
    uint64_t minimum_binding_size_bytes;
    uint64_t type_alignment_bytes, address_space_alignment_bytes;
    uint64_t runtime_array_offset_bytes, runtime_array_stride_bytes;
    uint32_t runtime_array_type, reserved;
} DoeCompilerResourceV4;

typedef struct {
    size_t words, types, members, resources, name_bytes;
} DoeCompilerTypedOutputCounts;

/* Record sizes must equal sizeof(the named V4 record) in this ABI. */
typedef struct {
    uint32_t *words;
    size_t word_capacity;
    DoeCompilerTypeV4 *types;
    size_t type_capacity, type_record_size;
    DoeCompilerMemberV4 *members;
    size_t member_capacity, member_record_size;
    DoeCompilerResourceV4 *resources;
    size_t resource_capacity, resource_record_size;
    char *names;
    size_t name_capacity;
} DoeCompilerTypedOutputs;

typedef struct {
    uint32_t schema_version, layout_profile;
    DoeCompilerNameV4 entry_point;
    uint32_t workgroup_size[3];
    uint32_t declared_struct_count;
} DoeCompilerModuleV4;

typedef struct {
    DoeCompilerTypedOutputCounts required;
    DoeCompilerTypedOutputCounts published;
    uint32_t required_is_exact, exhausted_pool;
    size_t minimum_pool_capacity;
    DoeCompilerModuleV4 module;
    DoeCompilerDiagnostic diagnostic;
} DoeCompilerTypedResult;

/* Query the loaded library. Failure leaves requirements unchanged. */
DoeCompilerStatus doe_compiler_typed_requirements(
    uint32_t abi_version,
    const DoeCompilerTypedCapacities *capacities, size_t capacities_size,
    DoeCompilerWorkspaceRequirements *requirements, size_t requirements_size);

/* Synchronous, no allocation, no retained pointers, no prepared handle.
 * flags selects WORDS and/or REFLECTION, optionally MEASURE. */
DoeCompilerStatus doe_compiler_compile_typed(
    uint32_t abi_version,
    const DoeCompilerTypedCapacities *capacities, size_t capacities_size,
    void *workspace, size_t workspace_size,
    const char *source, size_t source_length, uint32_t flags,
    const DoeCompilerTypedOutputs *outputs, size_t outputs_size,
    DoeCompilerTypedResult *result, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
```

### Record meaning and determinism

Reflection is declaration reflection, not per-entry reachability. Include every
named structure even if unused, all its transitive types, and every declared
resource and transitive type. Internal abstract/void/reference types and purely
local anonymous types not in this closure are not reflection declarations and
are not included. This does not omit any declared structure. Types shared by
multiple fields/resources are represented once; different nominal structures
remain different records. Resources remain declaration ordered.

Output indices are dense and meaningful only within this output, not semantic
TypeIds or target IDs. Assign named structs first in declaration order; discover
remaining concrete dependencies by iterative first-encounter traversal of those
structures' member types, then resources in declaration order. Forward indices
are legal; the complete graph is acyclic. Member spans are contiguous, ordered
by the owning struct and original declaration order. Scalars/vectors/arrays have
member_count0. Non-struct types have empty names. No implicit wrapper is reflected.

Scalar_kind uses only the NEW leaf constants and is zero for non-scalars.
Element_type is a valid output index only for vector/array/runtime-array,
otherwise NONE. Vector_width is2/3/4 only for vectors. Array_count is positive
only for fixed arrays, zero otherwise. Size is valid only with FIXED_SIZE;
unknown size is0 with the bit clear, not a zero-sized type. Stride is nonzero
only for array kinds. Runtime-array nodes and containing root structs have
HAS_RUNTIME_TAIL; neither is constructible. A runtime member's size is0 and its
type identifies the unknown-size case. Explicit attribute bits distinguish
absence from value; absent explicit attribute fields are0. All irrelevant and
reserved fields are zero, except defined NONE indices.

Resource kind/access reuse the unchanged storage/uniform and read/read_write
constants. Type_index names the source root store type, not a guessed scalar
and not the runtime element. Tail fields identify the actual runtime array and
its root-relative byte offset/stride; absent tail has NONE type and zero fields.
Both natural and required address-space alignments are reported independently.
Uniform validation has already succeeded under the reported conservative profile.

Name bytes are owned by the caller. Copy each nonempty name once per record,
without interning across records: entry name first, then named type names in
output type order, member names in output member order, resource names in
resource order. Every nonempty string has exactly one trailing NUL included in
name_bytes but excluded from its length. Empty names have offset0,length0 and
consume no bytes. Require total name bytes and every offset/length to fit u32.
Do not retain source/workspace pointers or publish C strings backed by source.

### Flags, counts and module fields

Only flag values1,2,3,5,6,7 are admitted. W denotes WORDS, R REFLECTION and M
MEASURE. `w` is the completed private module word count; `r` denotes the exact
(type,member,resource,name-byte) counts of section5's declaration closure.

| Flags | required on success | published on success | module on success |
|---|---|---|---|
| W | words=w, metadata all0 | equal required | all bytes0 |
| R | words0, metadata=r | equal required | schema/profile/entry/workgroup/struct count |
| W+R | words=w, metadata=r | equal required | same R module |
| M+W | words=w, metadata all0 | all0 | all bytes0 |
| M+R | words0, metadata=r | all0 | all bytes0 |
| M+W+R | words=w, metadata=r | all0 | all bytes0 |

Every successful row sets required_is_exact1. WORDS-only never exposes name
indices, including entry-point offsets, without a names span. In reflection
rows all nonempty module/record names point into that call's published names.
All rows execute the SAME semantic resolution, target-format checks and PRIVATE
SPIR-V lowering/emission into the same W staging pool. REFLECTION-only is not a
fast path: it can fail the same target/word budgets even though its public word
counts are0. MEASURE uses identical I/O/D/V/W limits and actual appends, not a
count-only escape from staging capacity. Internal checks do NOT run spirv-val,
subprocesses or any external validator; external validation is acceptance work.

The target-checked/emitted declaration set includes every named struct, every
resource, their transitive types, and every concrete type needed by validated
functions/IR. Dead/unused syntax is still semantically checked. All named struct
layouts must satisfy target Offset/ArrayStride/instruction limits even if unused
or only locally used. Uint64-reflectable but non-u32 target layout operands fail
UNSUPPORTED identically in every mode; reflection-only cannot admit a different
module. An otherwise huge fixed size alone is not an Offset/Stride overflow.

### Capacity discovery, preflight and atomic publication

1. Requirements query takes no source. NULL requirements returns INPUT_ERROR.
   Wrong version, capacities_size or requirements_size returns ABI_MISMATCH
   without writes and before descriptor dereference. With correct sizes, NULL
   capacities returns INPUT_ERROR. Validate reserved0, depth positive and within
   stated maxima, each pool's u32 index/sentinel domain, and every derived
   count/product/sum/alignment using checked arithmetic. Source offsets and name
   byte totals fit u32; indexed counts fit their LEDGER domains, including added
   primitive/sentinel slots. Zero pool capacities are legitimate limits.
   Query failure leaves requirements unchanged. Success returns current-library
   bytes/alignment (at most max_align_t); no stable private layout is promised.
2. Compile preflight precedence: NULL result -> INPUT_ERROR with no writes.
   Next check version, capacities_size and result_size; outputs!=NULL requires
   exact outputs_size, outputs==NULL requires outputs_size0. Any mismatch ->
   ABI_MISMATCH with every caller byte unchanged, before descriptor reads.
   If outputs exists, next read and check all three record_size fields against
   sizeof, even with zero capacities; mismatch has the same ABI/no-write rule.
   After these ABI checks, initialize result to zero. NULL capacities, invalid
   flags, reserved/depth/count/product errors, wrong selected-span shape or an
   invalid workspace return their ordinary error with diagnostic in result.
   MEASURE requires outputs=NULL; other calls require outputs!=NULL. Unselected
   spans must be NULL/capacity0. A selected zero-capacity span may be NULL; each
   nonzero span must be non-NULL/aligned. Source need not be NUL-terminated;
   existing source-span/embedded-NUL rules remain. Short/misaligned workspace
   fails before any workspace access. Check all capacity products before use.
3. Valid accessible objects and disjoint spans are CALLER PRECONDITIONS, not
   checked overlap failures. This includes the capacities and outputs descriptor
   objects, requirements/result object, source[source_length], workspace over its
   full supplied byte length, and each output over its FULL checked capacity,
   not merely the populated prefix. Zero-length spans have no extent. All live
   objects/spans in a call are pairwise disjoint, appropriately aligned and valid
   for the indicated read/write access. No runtime overlap-detection promise is
   made. Violating these preconditions, including result overlapping workspace
   or an output, is outside the API contract, so no no-write guarantee applies.
   Descriptor/source bytes stay immutable and valid until return; no concurrent
   observer/writer accesses active mutable buffers. Query inputs/output follow
   the same disjoint-object rule. No descriptor snapshot workaround is required.
4. Sufficient scratch lets MEASURE return the exact selected required counts
   with published/module zero and OK. It retains no state or handle. Recompile
   with the same immutable source bytes, flags except M, capacities and loaded
   library, plus newly allocated output spans: validation/target success and
   exact selected counts are identical. Different library/source/options/limits
   require a fresh result; pointer identity alone proves nothing. Workspace may
   differ if independently queried, sized and aligned for those same limits.
5. A normal call finishes exact selected counts before any bulk publication.
   Any selected short span returns OUTPUT_TOO_SMALL, required_is_exact1 and ALL
   selected exact required counts; published/module zero; every output byte
   unchanged. Zero-capacity/NULL spans may discover counts this way. Success
   initializes every byte, including padding, of populated records to zero
   before assigning fields; unused records and bytes beyond populated prefixes
   remain unchanged. Names/words likewise change only populated prefixes.
6. Scratch exhaustion reports first exhausted_pool and minimum_pool_capacity
   equal to the attempted append extent, a lower bound, NOT an exact final need.
   Set required_is_exact0, required/published/module zero. Retry may increase the
   named pool, query and repeat. Derived generic syntax uses POOL_TYPE_SYNTAX;
   its minimum is the needed TS records, increased via tokens per LEDGER.
   Source/token/syntax/metadata/IR/traversal/depth exhaustion uses INPUT_ERROR;
   private W exhaustion uses OUTPUT_TOO_SMALL. Other semantic failures preserve
   historical categories. Target-format limits use named UNSUPPORTED, not a
   fictitious growable pool. Other admitted failures also keep required/exact,
   published/module zero. All bulk output bytes stay unchanged on failure.
7. All output choices stage words privately. Keep type/layout/name source spans
   live through output admission; finish reflection counts/indices, target
   checks, legacy projection and numeric-state/errno restoration BEFORE the
   publication barrier. No fallible callback, allocation, validation, conversion
   or resolver remains after it. Publication is bounded copying and final
   success control fields; no second unchecked walk after words are visible.
   Resolved metadata need not have a duplicate staging array when copy cannot
   fail. Result/diagnostic are the intentional failure-report channel, not
   rollback-protected prior results. Atomicity is return-time failure atomicity,
   not cross-thread, signal, process-crash or hardware atomicity.
8. Workspace is reusable after every admitted outcome without reset. One active
   call per workspace; independent workspaces may run concurrently. No source,
   workspace or descriptor pointer is retained after return; output stays caller
   owned. No library allocation/fallback and hence no hidden OOM status. Caller
   or CLI allocation failure remains that owner's explicit failure.

## 6. Exact storage, phase and lowering ledger

MILESTONE_10_LEDGER.md is part of this frozen contract, not optional commentary. It gives
all proposed private record sizes/alignments, exact slice/lifetime formula,
compact compatibility profiles, source-derived auxiliary pools, target operation
rows and actual append/width/ID checks. layout_probe.c/.txt/.su retain the tiny
standalone GCC measurement in the isolated planning evidence. The probe source
is retained at contracts/m10_layout_probe.c; earlier planning revisions remain
unchanged in that evidence.

ABI1/v2 retain source65536,T2048,E320,S16,I385,W4096 and depth18/4. Type-syntax
depth is18 for fixed adapters and64 for the old job adapter. ABI4 uses its
explicit type_syntax_depth in1..64. Active expression/type parsing counters span
every mutual transition and are never reset by nested parser entrypoints. Their explicit
new pools are Q32, TS32, M64, R8, X=E+S+1, K64, L128, O=E, V97 and D464. Old
jobs retain their old source/T/E/S/W/depth admission and query overflow checks;
I=1+4E+8S and source-backed F/P slices remain unchanged, with those same compact
Q/TS/M/R/K/L/V/D constants and X=E+S+1,O=E. No Q32+T growth. Old scalar consts
remain inline; no repeated scalar annotation allocates a type. Old successful
scalar/flat-struct cases fit these pools by the explicit compatibility proof.
New aggregate inputs may exhaust a named pool without creating another grammar.

ABI4 has explicit Q/M/R/X/K/L/I/O/D/V/W pools and derives only source-backed
auxiliary slices. There is no target-operands pool: variable target operands
stream from existing owned spans into W after width/capacity checks. Primitive
TypeIds0..6 have no Q record; other IDs7+index. Iterative graph/layout traversal
never recurses in C proportional to a declaration/type chain. No fixed local
array, source-count-based constant expansion, hidden ID-indexed target table,
VLA or heap fallback fills a missing pool.

Measured proposed fixed workspace140056, alignment8, standalone wrapper frame
140080. Proposed ceilings: fixed workspace147456, ABI1 wrapper147968, all other
frames8192; combined fixed212992 and maximum-job196608 with LEDGER's explicit
recursive SCC and acyclic-chain budgets. These are reviewed safety envelopes,
not a permanent architecture or measured future compiler stack peak. Exact
implementation frames/call graph must independently fit them. V2/job requirements
may grow when queried from the current library; headers remain unchanged. No
allocation behavior change is proposed for the library. A failure to fit needs
an explicit pre-implementation contract amendment, not silent stack/heap growth.

Actual append-checked pools and actual prepublication ID/word counts decide new
aggregate admission. Conservative operation envelopes are safety accounting,
not a guarantee every new source below token capacity fits, nor a reason to
reject an old valid capacity vector. Preserve old scalar IR/word reservations
and capacity exemptions; width<=4 target safety expansion is separately bounded.

## 7. SPIR-V lowering seam

Use [SPIR-V composite instructions](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html)
with their actual index/type preconditions. Constant extract/insert paths require
valid literal indices; dynamic vector extract/insert requires a prior guard.
Dynamic indexing of an array value uses an owned Function temporary initialized
once at the source point, then a guarded pointer access. Temporaries are declared
at function entry but initialized on each dynamic execution, including loops.

Canonical constructible target types may carry the single validated layout;
no second uniform offset calculation exists. Wrap fixed resource roots in a
synthetic Block with one member at offset0 as needed; reflection exposes the
source root. Runtime-array resources use a direct runtime member in the target
Block. A runtime-tail root struct is itself the root Block variant, never nested
under another struct containing a runtime array. Such types cannot be by-value,
so they need no general logical-to-memory aggregate copy workaround. Function,
Uniform and StorageBuffer pointer variants remain typed and separately interned.
Any unavoidable distinct memory/value type variant must have a checked explicit
conversion plan and independent tests; SPIR-V1.4-only OpCopyLogical is not an
available shortcut under the retained1.3 target.

OpConstantNull handles typed zero without element expansion. Nonzero composites
use checked operand spans and shared child constants. A repeated component uses
repeated IDs only when actually serializing the required target instruction.
Do not flatten a giant source array just to get an indexable constant. Operations
may preserve vector shapes natively; a scalarized safety operation is bounded by
width<=4 and must preserve scalar numerical guarantees per lane. No eager
invalid lane operation followed by OpSelect. No new math accuracy claims.

Whole-aggregate OOB paths use the exact result type in OpPhi with a typed zero.
Labels/dominance and current block tracking must survive nested robust accesses,
short-circuit branches and helper calls; preserve M7-M9 termination/continuing
rules. Runtime array bounds use OpArrayLength on the correct root Block member,
not array length from another binding or an arbitrary struct child.

## 8. Legacy projection and CLI

Preserve old header files byte-for-byte and old exported function signatures.
Old successful scalar programs retain their semantics/reflection/diagnostics
and admission with their historical capacities. Identical admitted source must
produce identical SPIR-V across API modes where the existing contract promises
it and in ABI4 output selections; this does NOT add a cross-release SPIR-V byte-
identity promise for every historical source. Preserve actual frozen identity
requirements without broadening them.

ABI1 represents only its original storage runtime-array u32/f32 resources and
no declared structs/uniforms. V2/job represent only the old flat u32/f32 structs
and original resource shapes within existing field widths/counts. A scalar
store-root or i32/vec2 storage is not disguised as an old scalar array. Preserve
all-declared-struct reflection in declaration order, even when unused/local-only.
For unrepresentable successful semantic input return UNSUPPORTED with an
original-source diagnostic beginning `legacy reflection projection cannot
represent`, naming the first offending declaration. Perform this check before
emission publication; invalid source still fails its real earlier validation.
Legacy failure keeps word_count/reflection zero and every output word unchanged.

Local vectors/arrays, vector operators and aggregate helper signatures with
representable declaration/resource reflection are allowed by the SAME grammar.
Unused newly nested struct declarations fail legacy projection, not syntax.
Pressure is a proposed old-job/v2 witness if its actual limits fit, not ABI1
because it has Config. Divergence/project require typed reflection for vector
storage. No-reflection CLI must not accidentally retain the old-v2 projection
limit simply because current cli.c calls compile_job internally.

Propose binary-only CLI use ABI4 with WORDS only; no name/record/JSON allocations
when absent. Keep `--shader-path/--out` protocol and existing file rollback.
`--reflection` and `--reflection-v2` keep exact old schemas, headers and admission
projection. No `--reflection-v4` JSON option is selected for M10; typed native
reflection supplies the new projection without another schema surface.

Binary-only CLI uses LEDGER's explicit ABI4 profile: source65536,T8192,S1024,
E4096,W65536,depth32/16,Q256,M1024,R32,X5121,K1024,L4096,I24577,O4096,D7744,
V1281,type-syntax-depth32. These are reviewed limits, not every-source coverage.
CLI queries workspace, measures selected counts, allocates caller buffers,
recompiles and publishes only when both calls succeed. No automatic growth in
response to failing cases. Existing reflection modes retain old adapter profiles.
Every allocation/I/O failure keeps visible failure and owned cleanup/no-clobber
rollback; no process-crash atomicity claim.

## 9. Author-proposed acceptance outline (not independent obligations)

Before freeze: independently review this header/record contract, old projection
choice, literal/vector operator table, mixed dependency cycles, per-operation
capacity ledger, profile values/frames and natural/uniform layout rules. Use
the now-accepted M9 source/artifacts named in section1. Acceptance must preserve original
suites and explicitly list intentional former rejection lifts, not relax whole
classes or edit historical receipts. An author-proposed list is not independent
acceptance and no count is fixed here as a substitute for coverage.

A. **Small decisive vertical case.** Native C client measures and compiles a
nested struct containing vec3+scalar and a fixed array, copies it through a helper,
updates a dynamic element and writes a storage result. Independently packed raw
bytes and an instruction interpreter must establish correct fields/layout, not
read expected arithmetic from candidate reflection. Apply a valid wrong-offset,
wrong-stride, wrong-TypeId and wrong-guard mutant; each must be detected.

B. **Three unchanged witness sources.** Pressure, divergence and project from
`bench/kernels/`, through fresh source build, native library consumption and the
unchanged emitter-consumer protocol. Verify Config's eight fields/extent32,
vector storage stride8, helper snapshots, grid edges, width/height1, varied
non-square dimensions and padded dispatch, unequal resource lengths, signed
edge offsets and output sentinels. Preserve the input source hashes and each
stage's status. They do not prove arrays, nested structs or full type coverage.

C. **General language semantics.** Width2/3/4 and each leaf kind; explicit/inferred,
zero/splat/mixed-component/conversion constructors; abstract intermediate errors,
array inference/length equivalence, nominal non-equivalence, recursive and mixed
const/type cycles, shadowing, forward names, duplicate/missing members; locals,
let/const stages, nested aggregate formals/results, caller/callee snapshots,
constructor/call evaluation order, mutation during indices/RHS, loops/repeated
calls, all returns and dead-code validation. Reject invalid arity/shape,
aggregate arithmetic, runtime-sized values, mixed swizzle alphabets, multi-write
swizzles and every expressly separate family. Exercise all lifted old negatives.

D. **Layout/target validity.** Independent natural packing for vectors/arrays/
nested structs/explicit attributes; shared types in both address spaces; uniform
rejects unpadded scalar arrays/nested structs instead of repacking; explicit
padding makes the intended examples valid. Bool host-shareability, illegal
runtime-tail nesting/non-tail placement, size/stride/offset overflow, large
counts, integer literal boundaries and declaration-order independence. Compare
reflection, actual SPIR-V decorations and interpreted memory independently.
Run actual Vulkan1.1 spirv-val on emitted bytes, including mutated valid modules.
No claim from opcode existence, module magic or nonempty output alone.

E. **Robustness and identity.** Each reference step: first/last/one-past, negative,
INT_MIN, UINT_MAX, empty logical tail, truncating partial ranges, fixed/vector
index constant versus runtime; whole aggregate zeros and discard stores; nested
index load from another resource; OOB LHS still evaluates RHS; rvalue temporary
snapshot; writable/read-only/immutable categories; cross-helper resource effects.
Interpret original-source-independent initialized bytes; inspect forbidden pointer
formation before guards, resource identity and unaffected siblings/tails.

F. **Native/CLI ownership.** C and C++ consumers; unchanged old headers/symbols/
size/offset/status/JSON contracts plus exactly the two new exports; all selected
output modes; typed graph/name ownership after source/workspace overwrite; unused
structs included; old projection failure atomicity. Exact/short/zero/misaligned/
NULL/overflow capacities for each pool and each output independently, output
redzones/canaries, ABI-size no-write checks, measure versus compile count equality,
repeated workspace reuse after each failure and parallel independent workspaces.
Measure-only never publishes bulk bytes. Insufficient scratch never claims exact
output counts. API makes no allocation; CLI fails each allocation and I/O step
with owned cleanup/no-clobber rollback. Probe errno/FP state on all exits.

G. **Bounded-cost safety.** ASan/UBSan, malformed-source fuzzing, very long named
type/const dependency chains without host stack recursion, huge zero arrays
without leaf expansion, nested materialization DAG sharing, SPIR-V maximum
instruction-width boundary, target-ID/word capacity independence. Record actual
private record sizes, workspace, per-function/combined-stack bounds, fresh and
incremental native build/test cost and artifact size. Bounds are frozen BEFORE
implementation; a failure requests an explicit amendment, never silent growth.

H. **Historical and corpus ledger.** Rerun all accepted historical checks plus
independently accepted M9 cases, source-bound. Same98 paths/91 unique sources,
separate source/admission/emission/validation/instruction/native/backend columns.
No converting EMIT-SKIP into a pass, no source substring coverage arithmetic,
no qualification transferred between duplicate hashes. Expected new witnesses
are three paths only; any additional actual admission must earn its own evidence.
Workgroup/barrier/dot/floor/matrix/graphics/extension rejections remain named.

Frozen deliverables should name responsible implementation files/owners, exact
public exports and unchanged old JSON schemas, independent obligations and negative controls,
source/artifact hashes, tool versions/commands, status taxonomy, source-bound
consumer outcome and retained licensing. This planning pass supplies none of the
execution evidence and does not authorize acceptance or production promotion.

## 10. Freeze identity and implementation gate

The exact semantic/API tables, compatibility profiles, record-layout probe,
operation ledger and independent high-level obligations were reviewed before
this freeze. The final narrow parser-stack correction counts globally active
expression and type-syntax transitions; it changes no public record or ceiling.
Planning revision2 and its original seal remain preserved. The implementation
must independently establish actual behavior, record sizes, frames, call-graph
bounds and consumer evidence before acceptance. No candidate implementation or
execution result follows from this contract.

The sealed independent obligations originated from planning draft SHA-256
 d3e65bf7624b56716ee9f6805d900f19c119f267bfec6ffbd35b119060fb5c9b
and ledger SHA-256
 1554280166112044d815dc5bbd2660241298b28006bfab603b98a8351cf37fb7.
The reviewed parser-stack row above is the only subsequent semantic safety
precision; all other changes at repository installation are status/navigation.
CONTRACT_10.sha256 binds this contract, ledger, additive header, unchanged legacy
headers, independent obligations, reproducible layout probe and real witnesses.

The reusable next seam after acceptance is matrix shape/column-layout support in
this same TypeId/layout/access owner, not a second aggregate parser. It remains a
separate numerical/target/reflection acceptance increment. Full runtime metadata
allocation/clone/cache publication and native binding/lifetime integration remain
separate traced consumers, not discharged by standalone reflection.
