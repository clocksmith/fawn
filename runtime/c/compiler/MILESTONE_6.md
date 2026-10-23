# C compiler milestone 6: unified scalar contexts and compute entry interfaces

Frozen before implementation against accepted M5
`14aa666d2674a4de01233b74bf11bf7443d38158`. This increment removes the bespoke
index/condition expression model in favor of one typed scalar tree. It is not a
new shader recognizer, complete compiler, runtime migration or GPU qualification.

## Real sources and owner boundary

The [M5 ledger](../../../docs/c-migration/corpus-coverage-m5.json) records five
admitted paths / four unique sources from 98 paths. The unchanged next witnesses
are:

- `bench/inference-pipeline/kernels/gather.wgsl`, SHA-256
  `062496b48017bd03cf9be3535566c71d6f18b3c86e831ff3c533f8125e7e430f`.
- `bench/inference-pipeline/kernels/copy_buffer.wgsl`, SHA-256
  `5522b855dcf090259e0a6e3e9a529c4799295b49be65ef00b453b4dad76acd52`.
- `bench/kernels/compilation-corpus/trivial_noop_compute.wgsl`, SHA-256
  `0e88698096320b7a1e81a4ebe37897aebb71c3e6c9cfd085624e287e3c673073`.
- `bench/kernels/copy_textures_x32.wgsl`, SHA-256
  `263dbc4ad43346316cfe2c8badad06deb6025b6ebb687e3514b36d26c6883d10`.
- `bench/kernels/dispatch_noop.wgsl` and `draw_call_proxy_noop.wgsl`, both SHA-256
  `d51b69e09c71649b2266dc6f0538ee66a84ed7e433baf613ce71ef68320a8db8`.

The last four are actual no-op source files, not proof of texture/draw execution
suggested by some filenames. Preserve M5 binaries and all pre-change rejections.
Use unchanged native/CLI and existing emitter-consumer routes. Corpus admission
is not adoption by these applications or their actual source-to-device path.

## One scalar expression model

Retain M5 scalar operators, precedence, types, exact decimal materialization,
local snapshots, source-order statements, name resolution and explicit rejection.
Use the same scalar-expression grammar and typed IR lowering for:

- assignment RHS and immutable let initializers;
- every storage load/store index, including recursively nested storage loads;
- both operands of the existing six unsigned comparisons in if conditions.

Indices and comparison operands must resolve to u32. Permit parentheses,
constants, local scalars, uniform members, compute-input scalars, robust storage
loads and M4 +, -, *, max trees in these positions. The existing literal syntax
is retained: no new unsuffixed integer, signed, exponent, hexadecimal or broader
float domain. No casts or new scalar operators. Conditions still require exactly
one comparison, not bool values, logical operators, comparison chaining or
first-class comparison expressions. f32 indices/comparison operands reject even
if their values happen to be integral. Ordinary scalar type checks also apply in
unreachable statements. Vector/aggregate values and vector locals remain outside
this scalar owner.

Each nested storage access has its own length guard. An OOB token load produces
u32 zero, which may legitimately select a valid element of a second buffer; do
not propagate an invalid flag, skip later valid reads or suppress a valid store.
All u32 arithmetic wraps modulo 2^32, then the final index is independently
checked against that accessed binding. Never widen/clamp instead of wrapping.
Loads use actual emitted instructions, not reflected mode/source-pattern logic.
Source-order store effects and immutable snapshots remain unchanged. General
condition operands may create multiple robust merge blocks; the subsequent
comparison/selection must use the actual post-guard predecessor labels.

## Compute entry interface

The single compute entry may declare zero to five parameters, each with exactly
one unique supported builtin and a unique parameter name. Order and names are
arbitrary; optional trailing comma is allowed. Supported semantic identities:

- global_invocation_id, local_invocation_id, workgroup_id and num_workgroups:
  vec3u or vec3<u32>, with scalar .x/.y/.z reads.
- local_invocation_index: u32, read directly as a scalar.

Reject duplicate builtin identities, duplicate parameter names, wrong scalar or
vector types, unsupported attributes/builtins, vector-as-scalar use and components
on a scalar. Resolve parameter references by semantic identity/name/type, never
position or a hard-coded spelling. Parameters retain the existing module-shadow
behavior; body references resolve to the visible parameter, not a shadowed
resource/builtin. Ordinary new lets cannot shadow visible user declarations.
Type annotations are resolved in their proper declaration context.

Only the exact leading `let NAME = GLOBAL_PARAMETER.x;` retains the old zero-node/
zero-statement alias accounting and old alias-name rules. The parameter must carry
global_invocation_id, regardless of its position. Other components, other builtins,
parenthesized/annotated forms and later lets use ordinary scalar-local accounting.
No vector-valued local alias is introduced.

Allow zero resources and empty entry bodies as well as retained declared resources
and statements. All declared resource/struct reflection rules remain intact,
including zero counts and unused fields; ABI1 still rejects struct/uniform source.
Empty functions emit valid compute functions, not invented side effects or hidden
input requirements. SPIR-V entry interfaces contain only declared builtin inputs,
including unused declarations, with correct builtin/type decoration. No undeclared
GlobalInvocationId input is fabricated for a zero-parameter function.

workgroup_size accepts one to three integer dimensions with optional trailing
comma; absent y/z default to one. Each dimension is 1..256 and checked product
must be <=256. This is the current compiler subset bound, not device per-axis
qualification. Preserve all three dimensions in reflection and LocalSize.
The primary [WGSL indexing](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#access-expr),
[builtin](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#builtin-inputs-outputs) and
[workgroup-size rules](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#workgroup_size-attr)
underlie this subset. Other compute builtins remain excluded.
Builtin mappings follow [SPIR-V BuiltIn](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#_built_in):
NumWorkgroups 24, WorkgroupId 26, LocalInvocationId 27, GlobalInvocationId 28 and
LocalInvocationIndex 29. Input values are unsigned 32-bit scalars or three-component
vectors as above. No workgroup synchronization, subgroup, ordering or race claim.

## Derived storage limits and implementation ownership

All M5 admitted programs must remain admitted, except unchanged independently
reached output/source/token limits. A shared 64-node pool would not preserve this:
M5 excluded its separately represented index/condition atoms from the count.
Freeze a conservative unified scalar-node capacity of 320:

- at most 64 old RHS/initializer nodes;
- at most 3 index nodes beneath each old node: 64*3;
- at most 4 separate operand nodes per old statement: 16*4.

Total 64 + 192 + 64 = 320. This deliberately overestimates old reachable combinations.
Each scalar node lowers to at most one IR instruction; comparisons/control markers
add at most four per statement. At most one initial global-x/legacy-alias value
is exempt. Thus 1 + 320 + 16*4 = 385 preserves the current IR capacity and 386 value slots.
Every new builtin load/component value must be charged to an expression node,
not an uncounted multi-builtin prelude. Reuse may reduce operations, never increase
this bound without a separately reviewed amendment before implementation.

Unified expression depth is 18. M5's old maximum 16 may contain a load formerly
counted as depth 1; its old base-plus-offset index has depth 2, adding at most two
to the unified tree. Leaves count one, unary load(index) adds one, binary/max adds
one to the deepest child, and parentheses add one without another node. All
index edges count, so nested indirect accesses cannot evade the recursion bound.
The exact initial global-x alias keeps its existing exemption. Statement limit 16,
if-depth 4, source 65536 bytes, tokens 2048, names 63 bytes, resources 8, structs 8 by 8
members, and output 4096 words remain. Capacity errors are explicit, never truncation.
Some syntactically bounded shapes can still exceed the separate target-word cap.

Use compact typed source spans/IDs inside syntax expressions rather than copying
64-byte names into every node. Spans are offsets/lengths into the caller source,
validated before use; no pointer into phase-unioned tokens survives parsing.
Validated IR and published reflection own their names. No pointers escape a call.
Retain one parser/type pipeline and the phase-lifetime workspace union, not a
parallel legacy expression engine or generic arena/macro framework.

For this bounded increment, queried workspace <=96,208 bytes and ABI1 GCC14.2 -O2
static frame <=96,272; v2 entrypoint/core and library callees individually <=8,192.
Measure actual sizes/frames and recursive limits; proposed layout probes alone
are not acceptance. CLI caller-side frames remain separately reported. No combined
stack high-water claim. These limits protect this increment, not a permanent
allocation-free/full-compiler architecture. Future general job sizing/storage
and dynamic result surfaces have their own explicit migration gate.

## ABI, CLI and observable compatibility

Both public headers remain byte-identical. Retain all version/size/alignment/null,
no-write preflight, failure-atomic words/results, diagnostics, workspace reuse,
caller-allocation/no-library-allocation, concurrency and no-retained-pointer rules.
V2 workspace requirements describe the currently loaded library, not a permanent
layout. All three CLI routes, schemas, ordinary rollback and ownership remain;
same admitted storage-only source emits identical current SPIR-V across modes.
Old source semantics/ABI/metadata remain requirements. Compare retained binaries
for evidence, but target byte identity across compiler revisions is not a new
acceptance requirement where no published identity contract mandates it. No
production cache/compiler identity consumer is switched by this increment.

## Independent acceptance

Freeze cases before candidate inspection; preserve first failures and precisely
classify intentional grammar/boundary lifts. Do not relax malformed/type/access/
scope failures. One implementation owner plus independent evaluator, no parallel
runtime rewrite or extra tool prerequisites.

- Compile all six unchanged witnesses through fresh C API/CLI and existing
  emitter calls. Validate emitted modules explicitly as Vulkan 1.1. Execute gather
  and copy from independently packed raw resources and actual opcode behavior;
  no-op validation must also establish no storage effects or undeclared inputs.
- Indirect loads use unequal/empty logical buffers, nonzero sentinels, zero and
  UINT32_MAX tokens, overflow-to-valid-index products/additions, each first-OOB
  index, short source but long destination, uniform indices and load-valued
  conditions. Read-only buffers and unwritten outputs stay unchanged. Retain
  short-uniform dispatch rejection and per-binding storage robustness.
- Every builtin/component, reordered/renamed/multiple parameters, zero inputs,
  all workgroup dimension forms/products, mismatched types/duplicates, scoped
  names, nested conditions, source-order snapshots and dead-source errors.
  Whole-dispatch cases use coherent tuples: global=workgroup*LocalSize+local,
  local_index x-fastest, workgroup<num_workgroups. Separate synthetic uint32
  boundary injections from legal dispatch evidence. Use race-free or independent
  single-invocation cases, never interpreter iteration order as a GPU guarantee.
- Independent opcode mutations detect wrong builtin/component/resource identity,
  omitted nested guard, guard-before-wrap, suppressed valid post-OOB read/store,
  and incorrect merge/Phi predecessors. Match expected behavior independently
  of compiler reflection and separately verify the metadata/layout.
- Retained M4/M5 semantics/native/CLI/allocator/guard/concurrency checks; exact old
  worst-case accounting witnesses and new 320/321-node, 18/19-depth, 16/17-statement,
 4/5-if boundaries; output/source/token limits, malformed mutations, ASan/UBSan.
- Measure actual native C/C++ layouts, workspace/stack/callee budgets, strict
  source and prebuilt builds, exact artifact/source identities. Keep a portable
  selected-source suite, explicit installed tool paths, no hidden binary dependency.
- Update the same 98-path ledger with unique-output unchanged-consumer calls;
  distinguish actual first compiler phase, emission, independent target validation,
  opcode execution and unrun physical/runtime gates. Do not invoke gate main.

Remain on the isolated local branch. No production Zig change, public push,
installation or physical-device claim. Full compiler/targets/TranslationInfo/
cache/adoption/backend gates remain open; this is an owner increment toward them.
