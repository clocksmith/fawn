# M13: Compute-module owner contract

2026-10-23. Experimental C compiler owner; production/default remain Zig.
Baseline `665475907494d2095a760596218b4c2d363cb436`; accepted M11
`f01effdf070edf2f25cf0f2016c995df1e4d3bab`; published quality guide
`03d7a5cc311c46f0fa5df01f4e3383d126e3253c`. HEAD at investigation start was
`da9b75a09e93248f52749d5aa7dd414f4d8a0016`, M12 contract only. The original M12 numeric candidate was rejected for composed stack. Its author manifest
SHA256 was `0c01ff844b4ad98ef88439bf5d0cdf4ac9ad8cd6f22143de52fdf77aa9381a7a`.
M12 candidate2 is now independently accepted at manifest
`73b51c7355912608f71f3f19c44b7f57f17ef3f8b05be9fbf05bccd5331aaaf4`, acceptance receipt
`51ef417b1e339c1077a08336ed78514ad6ae5965c99fdf4d26cd5a9833a1749e` (parent confirmation).
Accepted M12 commit `e8c1b2ecb1f94bf0a8cf871835fed9038a0bc572`, tree
`624939bef7c91b3dc8623f3995d6b98fb393d0e6`, binds the unchanged accepted dependency bytes.
The seven-input design passed independent contract review. This installation changes
status/navigation and dependency identity only; implementation requires the independent
case seal. No implementation/test bytes changed and no compiler, native, package or GPU
build/test ran. The original isolated record-layout probe and planning seal are retained
in contracts/m13_layout_probe.c, contracts/m13_layout_probe.txt and
contracts/m13_planning.sha256; installation identity is contracts/m13_installation.json.

## Outcome and genuine boundary

One typed pipeline owns admitted compute modules, source-static per-entry interfaces,
all-entry SPIR-V1.3 and explicitly selected metadata. This is not selected-only
emission, another grammar, a CLI flag masquerading as module ownership, runtime
TranslationInfo adoption, or production replacement.

The decisive real boundary is `native/vulkan/vulkan_compute_native.zig:400-428`:
shader creation compiles/caches a whole module before a pipeline name exists.
At :555-560 the later pipeline supplies the name. Preserve both phases. The unchanged
`bench/gates/spirv_val_gate.py:126-145` command remains `--shader-path INPUT --out OUTPUT`,
with no entry argument, and must receive every exported compute entry in one binary.
The sharper CPU metadata witness is `backend/metal/metal_runtime_resources.zig:223-243`:
second entry `selected`, workgroup (7,1,1), group1/binding2, minimum28, helper rejected.
These are requirements to reproduce through the new C API, not permission to replace
that MSL runtime consumer or claim its adoption. See MILESTONE_13_EVALUATION.md for exact witnesses.

## Additive ABI and adapters

`include/doe_compiler_module.h` declares ABI/schema5 and exactly two new exports; the old
seven symbols and all old public layouts remain intact. Reuse V4 type/member/resource
records unchanged. Add compute-entry and declared-workgroup records, owned names,
and a flattened uint32 reference span. Entry resource/workgroup ranges identify the
appropriate declared table. No heap handle, callbacks or global mutable state.

Selection is mandatory explicit request data:
- NONE: no selection, all entries emitted, selected_entry=NONE in published metadata.
  This is shader-module creation, not WebGPU's omitted pipeline name.
- AUTO: an omitted pipeline name; exactly one compute entry succeeds, two or more
  fail SEMANTIC_ERROR. No preference for `main`, source-first or source-last.
- NAME: 1..63 exact case-sensitive bytes must name a compute entry under the current
  identifier envelope. A helper or
  unknown name fails SEMANTIC_ERROR; no entry-function coercion. Empty/NULL-positive,
  embedded NUL, invalid mode/reserved or wrong shape fails INPUT_ERROR.
- NONE/AUTO require NULL name/length0. Wrong stage syntax remains the existing explicit
  UNSUPPORTED at that source attribute, in every selection. Graphics is not admitted.
- A source with no compute entry retains a parse rejection at source end. Supporting
  library-only/zero-entry SPIR-V modules is not claimed by this compute-artifact owner.

Validate the entire admitted source before resolving a selection. Module errors in
unused helpers, sibling entries, dead statements and constant-skipped operands cannot
be hidden by NAME/AUTO. No external layout/device is supplied, so this API does not
invent pipeline-layout/device-limit validation. Identical source/capacities/library
produces identical module words under NONE, AUTO and every successful NAME request.

Old ABI1/v2/job always require singular reflection. They retain a one-entry projection
storage envelope; encountering a second completed entry rejects UNSUPPORTED with an
explicit singular-projection diagnostic at its name, never selects/truncates. This
adapter rejection can precede validation of unrepresentable remainder; it is not a
claim that remainder is valid. Shared parsing/validation logic is otherwise identical.
ABI4 allocates module-capable storage regardless of flags. WORDS-only and WORDS+MEASURE
accept modules, return full-module words/counts and keep every module/reflection byte
zero. ABI4 REFLECTION (including MEASURE+REFLECTION) rejects multiple entries as an
unrepresentable projection after full module validation. Historical singular success,
declared reflection ordering, names, numeric/diagnostic and output rules survive.
Old JSON schemas remain singular. Intentional old rejection lifts are individually
listed in evaluation, never broad test deletion. No ABI is reinterpreted in place.

## Typed representation and semantic ownership

Keep FunctionRecord bodies, signatures, helper FormalRecords, typed IR and existing
resource/layout records. A tagged function union reuses its formal_start slot for an
entry_index only when FUNCTION_ENTRY is set; entries have formal_count0. Each compact
EntryRecord points to a distinct shared EntryInput range and owns its workgroup values,
closure ranges and logical workgroup bytes. Entry inputs are not helper formals or
mutable locals. Current function identity selects its entry, never a singleton entry.
The five currently admitted builtins remain ordinary per-entry unique semantic kinds;
aggregate storage grows with all inputs, not a private five-element module array.

Replace callee-only scratch with typed SourceReference {kind,index} slots for each
physical Expression and each assignment Statement root. Kinds distinguish none, user
function, workgroup barrier, resource, workgroup root and entry input. Record resolved
identity on every semantic visit, including emit=false; repeat visits must agree.
Use existing lexical resolution, not spelling or a second resolver. Assignment roots
need their own slots because not every root has an Expression node. Record reads,
writes, compound updates, member/index roots, call statements and calls in skipped
operands. Arguments retain source order; this owner does not replay/fold source anew.
Module annotations/constant initializers still receive complete ordinary validation;
no valid resource reference can hide in an admitted const/type attribute.

After whole-module cycle validation, traverse each entry's static call closure
iteratively, using the existing function graph fields. Include every source call in
its reachable functions, even after return or in a constant-skipped expression.
Every function is visited once per entry. Mark resolved resource/workgroup IDs,
then append each distinct declaration in declaration order to the private flattened
reference pool. No entry×function/resource matrix and no recursive call-graph DFS.
Sort a scratch copy of resource indices by (group,binding,declaration index) to detect
collisions; never sort/mutate the declaration table or published source order.
All declared resources/structs remain reflected, including unused declarations.
V5 additionally reflects all declared workgroup roots and their type closure; V4's
meaning and type closure are not silently enlarged. Entry closures reference those
declared tables, not duplicate/pruned resource definitions or target-variable IDs.

Two distinct resource declarations may share a group/binding pair in disjoint entry
interfaces. Two in ONE entry closure reject the module, even with identical layouts
and even if a different entry was selected. This is a source-detectable shader error,
not a deferred pipeline compatibility error. The dated WGSL §13.3.2 normative rule
links to shader-creation error; §13.3.3 separately covers supplied pipeline layouts.
Emit and validate every valid entry; neither a failed sibling nor recursion in an
unused helper disappears. Shared helpers cannot capture a sibling's input or locals.
Uniformity summaries are shared per helper, but entry builtin seeds and callsite
contexts are per entry. Analyze all entries and all module-detectable errors using
the accepted M11 source/uniformity owner. No first-entry or emitted-only shortcut.

## Artifact and per-entry workgroup behavior

Emit each validated function body once, reserve callee IDs before bodies, and emit
one OpEntryPoint and LocalSize mode per compute entry with its actual name/triple.
SPIR-V remains 1.3 / Vulkan1.1. Each interface lists the Input variables actually used
by that emitted entry/call tree, including its hidden initialization input when needed.
Do not append storage/uniform/workgroup globals under SPIR-V1.4's broader rule.
Distinct entry-input target IDs prevent sibling identity/type/builtin-order leakage.
Public declared/static builtin masks describe source, not synthetic initialization.

For every entry with a nonempty source-static workgroup closure, use its own declared
LocalInvocationIndex target, or synthesize one if absent. Initialize exactly that
closure's whole typed roots, then execute the accepted workgroup barrier before any
user code or early return. No per-element host flattening; no sibling-only root stores.
Each initialization has 4 target IDs and 26 control words, plus 3 words per root store.
The per-entry byte value is checked sum(roundUp(naturalAlignment, typeSize)) for its
unique roots, not a device's physical allocation, a dispatch bound or cache obligation.
All declared roots remain independently type/layout validated. Every entry's prologue
must work across groups and successive dispatches with hostile initial memory.

Existing admitted scalar/vector/aggregate/numeric/control/robustness semantics remain.
Matrices, pointers, textures, graphics, overrides, extensions and other targets are
outside this owner, not permanent language exclusions. Existing 63-byte identifiers,
group0..3/binding0..31 and workgroup dimension/product1..256 remain the currently
published capability envelope here, not new fixed architectural requirements. The new
representation uses spans/u32 values and checked streaming, so later owner-reviewed
lifts need not replace it. No witness-specific capacities or automatic growth/retry.

## Counts, lifetime and failure publication

WORDS/REFLECTION, with optional MEASURE, admit flags1,2,3,5,6,7. Every successful mode
runs the same full validation, closure, target planning and private emission. REFLECTION
is not a no-emission escape; MEASURE needs the same configured pools, including words
and private closure references. Required counts are exact only after all fallible work.
WORDS-only zeros all metadata counts/module; REFLECTION-only zeros public word counts.
MEASURE publishes no bytes/module. Published reflection owns copied names, record values
and indices; source/workspace/selection may be overwritten immediately after return.
Names are entries in source order, then existing type/member/resource name order, then
workgroup names. Nonempty names get one trailing NUL; no interning, no borrowed names.
Each type/reference/name index and total fits its declared uint32 domain.

Preflight follows V4: NULL result before writes; exact ABI/capacity/selection/output/
result sizes and all record sizes before descriptor-dependent mutation; then zero result.
Correct-size NULL descriptors, invalid flags/spans/capacities, short/misaligned workspace
are ordinary INPUT_ERROR, before workspace access as applicable. Query failures leave
requirements untouched. A NAME lookup diagnostic has no invented source span: unknown
uses offset0; a helper uses its actual declaration offset. Module errors retain original
source offsets/line/column. Diagnostic text is caller-owned, bounded and stable.

Source, descriptors, selection-name bytes, workspace (full supplied extent), result and
all output full-capacity spans must be valid/aligned/pairwise disjoint until return.
This is a caller precondition, not a runtime alias-detection promise. Unselected spans
must be NULL/capacity0; selected zero-capacity spans may be NULL. Check byte products
before pointer formation. No reset, retain/release or destroy operation is introduced.
Independent workspaces permit concurrency; no active mutable buffer may be shared.

A selected output short by one returns OUTPUT_TOO_SMALL, exact required counts,
published/module0 and untouched output bytes. Scratch exhaustion reports its first
pool and attempted append extent (a lower bound, not exact final need), with required/
exact/published/module0. New entry/input/reference pools use INPUT_ERROR; word staging
uses OUTPUT_TOO_SMALL; format exhaustion remains UNSUPPORTED. No truncation.
All graph walks, target reservations, reflection maps, selection/projection checks and
numeric environment restoration finish before publication. Publish only prevalidated
bounded copies/assignments, with zeroed populated record padding; spare output stays
untouched. No graph/resolver pass after words become visible. Preserve errno and caller
FP state on every admitted exit under accepted M12 rules, including failure/reuse.

## Gate deliberately left open

The compiler result is not TranslationInfo. Runtime adoption must separately version
cache/compiler/config identity; carry every entry's workgroup/resource metadata; preserve
needs_sizes_buf and dispatch obligations, clone/free/deep names/partial OOM rollback;
transfer correct selected metadata shader→pipeline; and reject stale/corrupt cache entries.
Do not fabricate empty universal obligations or claim Metal/Vulkan/D3D12/package execution
from CPU reflection. Production/main/default stay Zig. Physical and package gates remain
unchanged. No reserved labels/error-scopes work, software installation, publication, security change,
private trace retrieval or protection-boundary change is part of this owner.

Primary interpretations: [dated WGSL](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#resource-interface),
[dated WebGPU entry selection](https://www.w3.org/TR/2026/CRD-webgpu-20260915/#programmable-stages),
[SPIR-V specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html).
The completed source trace and its manifest are bound in contracts/m13_source_inputs.json; no old
report is imported as implementation acceptance. Review MILESTONE_13_LEDGER.md and MILESTONE_13_EVALUATION.md
before implementation; the independent case seal remains required.
