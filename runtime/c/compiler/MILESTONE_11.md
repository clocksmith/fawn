# M11 frozen contract: typed workgroup state and uniformity

2026-10-23. Frozen implementation contract, not implementation acceptance. Baseline is independently
accepted M10 `2220c64cff0bd87b1eed6b246515941ce7d18d24`, isolated branch
`experimental/c-migration-dot-20261023`. This owner was independently reviewed and sealed before implementation. Production Zig/default/main and historical evidence stay.

## Decision and real boundary

Implement ONE shared owner: fixed-footprint typed workgroup roots, their default
initialization, `workgroupBarrier()`, and transitive workgroup uniformity across
the already admitted compute/control/call language. No shader/source recognizers,
per-API grammar, mechanical translation, generic graph/arena framework, new target,
runtime adoption or physical-GPU/performance claim.

The current caller is the binary-only CLI (`--shader-path` / `--out`) through the
shared typed compiler. It does not consume runtime TranslationInfo. Keep all FOUR
public headers byte-identical and all SEVEN exports. No new ABI/version or public
projection is needed here. ABI4 remains named-struct plus descriptor-resource
closure, NOT reflection of every module root or dispatch requirements. A workgroup
root never occupies a resource slot, group/binding coordinate, or resource count.
Workgroup-only anonymous types need not appear in ABI4; named structs still do.
Old reflection projections apply their existing representability rules to exactly
the same descriptor/named-struct surface, not to hidden workgroup variables.

This choice does not erase host metadata: emitted Workgroup declarations are the
current standalone artifact. This compiler computes checked logical type/aggregate
storage totals internally, but these are not a physical-device allocation size.
Before ANY runtime adoption, a separately reviewed, versioned metadata boundary
must expose reachable workgroup requirements, device-limit checks and target layout
as required by that caller. Existing ABI4 structs cannot be grown in place or
claimed to provide those data. That later gate is open, not silently fulfilled.

contracts/m11_source_inputs.json binds 67 current sources, all equal to the prior trace where
compared. The trace's candidate status is superseded only by accepted M10; its
original files are untouched. Current same-98 source ledger: 37 admitted/executed,
61 explicit rejects. Workgroup-first classification (34 paths) is a dependency
inventory, NOT a promised admission count. Unrelated runtime work in the shared
checkout is not part of this owner.

## Exact language boundary

Retain all accepted M10 semantics/capacities/projections except named intentional
new admissions and current-library private workspace bytes described in MILESTONE_11_LEDGER.md.

- Module `var<workgroup> name: T;` accepts concrete bool/i32/u32/f32 scalars,
  vectors, positive fixed arrays and nominal structures recursively composed of
  them. Resolve types/counts/attributes with the existing declaration owner,
  including forward declarations, shadowing and mixed dependency-cycle checks.
  No initializer and no source access-mode argument are valid on workgroup vars.
  Runtime arrays, runtime-tail structs, abstract store types and nonconstructible
  elements are invalid here. Atomic/pointer/override-sized and other excluded
  type families remain UNSUPPORTED where they are valid WGSL outside this owner.
- Names are hoisted in the existing module namespace. Root lookup obeys ordinary
  nearest local/formal/module precedence. Helpers can read/write module roots;
  their parameters/results are still by value, never source references.
- Workgroup roots have independent per-workgroup identity and lifetime, shared by
  the group's invocations. They are not Function storage, descriptors, Private
  globals or aliases across groups. Source copies still snapshot whole values.
- Existing typed reference access, single-component stores, whole-value stores,
  compound-assignment evaluation order and independent per-step bounds apply.
  Out-of-bounds read is typed zero and store discards, after required evaluation.
  This robustness policy does not make racing accesses defined. Concurrent writes
  to components of the same vector can conflict on that vector's memory locations;
  the compiler/evaluator may not invent independent scalar race granularity.
- Add only no-argument/no-result `workgroupBarrier()` as a builtin call statement.
  Normal module/lexical lookup precedes builtin dispatch. Helpers may contain it.
  It is neither constant nor a value. Wrong arity/use is a semantic error, not a
  dropped no-op. Source synchronization is statically checked even without roots.
- Single compute entry, admitted five input kinds, source dimension/product
  envelope 1..256, numerical and target profiles remain unchanged.
- storageBarrier, workgroupUniformLoad, atomics, source pointers, private globals,
  aliases, overrides, multiple entries, vector math builtins, matrices/f16,
  subgroups/textures/graphics and non-SPIR-V targets remain separate owners.
  Valid unimplemented source is UNSUPPORTED, not presented as invalid WGSL.

## Initialization and target behavior

Use SPIR-V 1.3, Logical/GLSL450 memory model and default Vulkan 1.1 validation.
Do not add VulkanMemoryModel capabilities or platform extensions in this owner.

Each declared root has an actual module-scope Workgroup OpVariable, typed pointer,
and compact typed zero declaration. Emit every declared root (including unused
ones), as M10 emits declared resources. No descriptor decorations or synthetic
storage block wrappers are applied to these roots. Access-chain pointers carry
Workgroup storage class; natural type facts and existing per-step guards remain
shared with other typed references. Source parameters/reflection do not acquire
synthetic inputs.

When there is at least one workgroup root, the entry begins, AFTER Function variable
declarations but BEFORE every source operation/call/early return, with this fixed
compiler-owned prologue:

1. Read LocalInvocationIndex; reuse its source input when present, otherwise add
   one private target input/interface entry (not a sixth source input).
2. Lane index 0 alone writes one whole typed zero value to each root. Use
   OpConstantNull and whole-root OpStore, not a host-generated flattened list or
   cross-lane scalar-component writes. All roots are finished before merge.
3. Reconverge, then every invocation executes one OpControlBarrier with execution
   scope Workgroup (2), memory scope Workgroup (2), semantics
   AcquireRelease|WorkgroupMemory (0x108).
4. Execute the source body. Source early returns cannot bypass step 3.

The prologue executes once per invocation/group/dispatch, never at helper entry.
There is NO compiler prologue/input when there are no roots. It is retained for a
one-invocation workgroup for a simple exact boundary. Source barriers emit the
same exact OpControlBarrier, including barriers in helpers/loops; each dynamic
instance matters. Neither source nor generated barrier fences storage buffers or
synchronizes different groups. Do not infer uniform load values from a barrier.
No driver-zeroing, CPU-model-zeroing or extension-based initialization assumption.
No speed claim for serial lane-0 initialization; cooperative optimization is later.

## Uniformity: required analysis, not retained-gap parity

Normative anchor: WGSL CRD 2026-10-21, sections 7.3, 9.7, 15.2.3/5/6/7/8 and
17.11, https://www.w3.org/TR/2026/CRD-WGSL-20261021/ . The workgroup-scope dependency
rules of that dated version define acceptance. Synchronization uniformity failure
is an error with no diagnostic-filter escape. Retained Zig lacks an identified
analysis, so its acceptance alone is not an oracle. The bounded representation
below is an implementation proposal for those rules, not new language semantics.
Analyze the retained VALIDATED SOURCE AST, not runtime-folded IR: constant-skipped
short-circuit RHS expressions/calls still participate under the dated rules.
This differs from the explicit no-Next statement exclusion. Preserve runtime
short-circuit emission; do not execute its skipped RHS merely to expose analysis.
The resolution/behavior retention, storage and work bounds are in MILESTONE_11_LEDGER.md.

Run after complete typing/behavior/call-cycle checking and before target planning.
Use all validated function bodies, including unused functions. Follow the dated
behavior rule for statically unreachable statements: they remain name/type/grammar
validated but do not contribute dependency edges after a no-Next statement.
Compiler-generated zeroing is a separately proven uniform prologue, not a fake
source barrier that makes all user source require uniform entry.

The logical graph has value, reaching-local, control-flow, RequiredUniform,
MayNonUniform, entry-CF, formal and return nodes. A dependency edge is implication;
failure means RequiredUniform reaches MayNonUniform. It is acceptable to solve
this graph without materializing an adjacency arena: MILESTONE_11_LEDGER.md freezes a Boolean
sensitivity/structured-fixpoint implementation with bounded storage. It must match
the normative graph, not a 'once varying, always varying' taint shortcut.

Closed transfer obligations:

- Constants/read-only roots and workgroup_id/num_workgroups start with current
  control dependency. local/global invocation IDs and local index are intrinsic
  varying sources. Uniform and read-only storage loads additionally depend on all
  index expressions; mutable storage and ALL workgroup loads are intrinsically
  potentially varying, even same-address reads immediately after a barrier.
- Local values/let snapshots/formals carry value dependency plus control dependency
  at use. Whole-local assignment kills the previous value dependency. Partial
  struct/array/vector assignment retains prior whole-root dependency and includes
  RHS, current control and index dependencies. Aggregates are one conservative
  uniformity value, matching the dated analysis, not independent-component facts.
- Unary/binary/composite/access operations propagate their operands. Short-circuit
  RHS is analyzed under the LHS value dependency; it is not eager. Apply source
  control dependency even when runtime IR reuses a let/formal/constant ValueId and
  therefore has no new instruction for that source read. Constant folding is
  NEVER a uniformity proof: `false && varying` retains the varying dependency,
  and a constant-skipped helper call retains its call-site uniformity requirement.
- Both sides of if are analyzed. Resulting control reconverges to incoming CF ONLY
  when the statement behavior is exactly {Next}; otherwise outgoing control joins
  both branches. Local next-values join only branches that can reach Next.
- Loop head depends on entry CF and the applicable body/continuing backedge CF.
  Loop-local reaching values join entry and live backedges to the least fixed point.
  Continue feeds its correct continuing block, break its correct loop exit; normal,
  continue, break and return channels remain distinct. A varying break/continue
  can invalidate a barrier on a later iteration even if the first is uniform.
  Use actual behavior sets, including continuing break-if; do not use trip-count
  guesses or execute source numerics to prove control uniformity.
- A barrier-free loop with whole-loop behavior {Next} reconverges to incoming CF,
  even when lanes execute different iteration counts. This is required for the
  unchanged matmul witness. A path that returns while other lanes continue cannot
  be forgotten when checking a later barrier. Neither 'all loops reconverge' nor
  'no varying loop ever reconverges' is acceptable.
- Analyze callees before callers using the already acyclic call graph. Summaries
  retain uniform-callsite requirement, intrinsic return variability, and each
  formal's required-uniform / return-dependency bits. Use these at each actual
  call, with all argument effects in source order. Caller CF reconverges after a
  function call as specified; a helper return does not terminate the caller.
  Mutable module loads remain varying across calls. No recursive callee expansion.

One deterministic first error identifies the original builtin/helper-call source
location and says uniform control cannot be established. Existing diagnostic
record/status/offset/line/column/message capacity stays; no trace API or warning
channel is introduced. A full causal graph diagnostic is not promised. Failure
from the normative rules is SEMANTIC_ERROR; genuinely valid-but-unimplemented
analysis/target cases are UNSUPPORTED with an explicit reason, never success or a
false 'invalid WGSL' claim. The listed language/control family is REQUIRED, so an
unsupported fallback cannot be used to evade its acceptance tests. The one explicit
analysis-resource exclusion is the frozen, counted work ceiling in MILESTONE_11_LEDGER.md;
exhaustion is UNSUPPORTED, not a proof of invalidity. Mandatory witnesses must fit.

## Source-bound mandatory witnesses

Hashes are full in contracts/m11_source_inputs.json, source files must remain byte-for-byte.
These SIX small paths cover distinct aspects; count is not the entire admission
promise. The whole same-98 ledger must still be freshly rerun and classified.

- bench/kernels/zero_initialize_workgroup_memory_2048.wgsl: 256 lanes, 8192 logical
  shared bytes, no source barrier, all 2048 meaningful f32 outputs exactly +0.
- bench/kernels/workgroup_non_atomic.wgsl: 256 lanes, integer indexed cross-lane
  shared reads, two source barriers; exact independently computed u32 outputs.
- bench/kernels/workgroup_non_atomic_1024x100.wgsl: actually 256 lanes, 100 reuse
  iterations and 200 dynamic source barriers. Do not derive size from filename.
- bench/inference-pipeline/kernels/dot_product_pairwise_f32.wgsl: 8 lanes, no dot
  builtin, guarded partial input, short circuit and continuing reduction phases.
- bench/inference-pipeline/kernels/matmul_logits_tree64_f32.wgsl: 64 lanes,
  workgroup-uniform early return, varying barrier-free accumulation loop, uniform
  reduction loop. Use cols unequal to 64 and an out-of-range whole workgroup.
- bench/kernels/monte_carlo_reduce_vec4.wgsl: 64 lanes, vector shared elements,
  partial final input group, barrier before a uniform break; whole-vector ownership.

The zero-init and pairwise source outputs are not group-indexed. Execute their
unchanged witnesses with ONE group per dispatch and repeated dispatch, not an
invalid multi-group race. Use the other group-indexed witnesses and dedicated
independent fixtures for multiple groups/shared-group isolation. Multiple groups
are an overall acceptance obligation, not permission to race a particular source.

## Independent acceptance and stop conditions

Freeze exact fixtures, input bytes, expected semantics and controls BEFORE
candidate inspection/execution, separately from author developer tests. Preserve
all first failures, repairs, disclosure and source/manifest/contract identities.

1. Fresh C11 CLI/library build and default Vulkan 1.1 validation of exact emitted
   bytes. Inspect Workgroup types/pointers/roots, no descriptor masquerading,
   zero stores/order and exact barrier scopes/semantics. Extend the single shared
   parser/sema/IR/emitter; no oracle source import or host shader execution.
2. A separately owned instruction executor must maintain separate invocation
   frames/call stacks, per-group shared state and per-dispatch lifetime. Dynamic
   barrier rendezvous includes static instruction, caller chain, loop phase and
   workgroup identity, not a static opcode counter. All required invocations
   arrive before release; early-exit/mismatched phase/deadlock is detected.
3. Run fair round-robin, reverse, seeded shuffle and bounded-burst schedules,
   repeated dispatch over poisoned reused shared memory, more than one group,
   unequal input/output lengths and partial groups. CPU workgroup memory starts
   nonzero/poison, never implicitly default-zero. Maintain cross-invocation
   read/write happens-before/race checking; order only Workgroup memory at this
   barrier. Do not treat racy source as a positive numerical witness.
4. Oracle self-controls remove init, remove/skip barriers, privatize shared roots,
   alias distinct groups, change memory/execution scope or semantics, and mismatch
   dynamic helper/loop phases. Each must fail the intended check. Include a
   storage-buffer race control proving this barrier is not a storage fence and a
   same-vector component-write conflict control. Distinguish validator-invalid
   mutations from validator-valid semantic controls; do not count either as GPU.
5. Independent uniform/nonuniform pairs cover all listed transfers, function
   chains, partial assignments, mutable/read-only roots, constant-skipped and runtime
   short circuit (including syntactically present barrier helpers), return,
   break/continue/continuing/backedges and valid reconvergence. A small explicit
   dependency-graph oracle is preferable to copying the proposed Boolean solver.
6. Use exact integer/zero and controlled representable f32 inputs where possible;
   independently derive source reduction order. Preserve accepted numerical
   classes for the rest. No universal GPU bit identity claim.
7. Native source/archive/shared/prebuilt C/C++ consumption, current query and stale
   workspace rejection, exact/short/disjoint spans, repeated/concurrent calls,
   no allocation, fenv/errno, failure atomicity and ASan/UBSan. WORDS/REFLECTION/
   MEASURE still perform the same semantic/uniformity and private-emission work.
8. Reproduce historical accepted M10/M9/M8/earlier successes, capacity and control
   detection. Enumerate exact new positive replacements for old workgroup/barrier
   negatives and query-size assertions. Original tests/evidence remain unchanged;
   no class-wide skip or relaxed outcome set. Publish fresh same-98 per-source
   results, not rejected or unrelated prebuilt counts as new coverage.
9. Verify actual private records, phase lifetimes, target counts and `.su` call
   graph bounds against MILESTONE_11_LEDGER.md. If an implementation needs a larger envelope,
   obtain a reasoned pre-candidate amendment; do not rewrite limits after failure.

Acceptance stops at standalone compiler/CPU-model/target validity. No physical GPU
exists here. Vulkan/Metal/D3D12 runtime adoption, hardware behavior, performance,
native metadata and whole-Doe migration are NOT established by this milestone.
