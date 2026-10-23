# Independent acceptance design

No acceptance executed. Freeze independent tests and their oracle/source hashes before
implementation. This document is the reviewed requirement/witness set, not a test seal.
Retain all failed attempts and repaired identities.
Current numeric candidate2 manifest hash is
`73b51c7355912608f71f3f19c44b7f57f17ef3f8b05be9fbf05bccd5331aaaf4`, independently
accepted with receipt `51ef417b1e339c1077a08336ed78514ad6ae5965c99fdf4d26cd5a9833a1749e`.
Accepted M12 commit `e8c1b2ecb1f94bf0a8cf871835fed9038a0bc572`, tree
`624939bef7c91b3dc8623f3995d6b98fb393d0e6`, and unchanged dependency bytes are now bound.
Do not edit historical receipts, the 98-source corpus, retained native tests or existing
M12 tests to obtain an entry-point success count.

## Decisive boundaries and exact oracles

1. Ordinary GCC/C++ client compiles ONE source module with two asymmetric entries,
   consumes copied metadata through ABI5 and emits ONE SPIR-V file. The independent
   decoder enumerates all OpEntryPoint records and LocalSize modes by function ID.
   Execute every name from that same binary, independently of compiler reflection.
   NONE versus each NAME must produce identical words. AUTO must reject this module.
2. Unchanged binary consumer: `bench/gates/spirv_val_gate.py:126-145` calls the existing
   binary CLI without selection. Do not substitute an entry-aware wrapper. Same CLI
   ABI4 WORDS-only route must emit all entries and preserve its existing I/O failures.
3. Retained named metadata boundary: `metal_runtime_resources.zig:223-243` source is:

```wgsl
@group(0) @binding(0) var<storage, read_write> unused: array<u32, 2>;
@group(1) @binding(2) var<storage, read_write> selected_data: array<u32, 7>;
@compute @workgroup_size(1) fn main() { unused[0] = 1u; }
@compute @workgroup_size(7) fn selected() { selected_data[0] = 2u; }
fn helper() {}
```

   Exact declared resources are both records. Entry `main` references index0 and
   workgroup(1,1,1); `selected` references index1 and workgroup(7,1,1), group1/binding2,
   minimum28. `helper`/unknown fail selection. This is metadata-only: executing all
   seven invocations writing selected_data[0] would introduce an unnecessary race.
   Running retained Zig test-core later is comparison evidence, not C runtime adoption.

The independent asymmetric EXECUTION witness differs intentionally:

```wgsl
@group(0) @binding(0) var<storage, read_write> first_data: array<u32, 2>;
@group(1) @binding(2) var<storage, read_write> second_data: array<u32, 7>;
@compute @workgroup_size(1) fn main() { first_data[0] = 1u; }
@compute @workgroup_size(7) fn selected(@builtin(local_invocation_index) lane: u32) {
    second_data[lane] = 100u + lane;
}
fn helper() {}
```

   Start both buffers with 0xdeadbeef. One selected workgroup changes second_data to
   [100,101,102,103,104,105,106], leaving first_data untouched. One main workgroup
   changes only first_data[0] to1, leaving first_data[1] and second_data untouched.
   Fresh buffers per named run. Integer exactness; no tolerance. Select first, second,
   a source-permuted second, and a module with no entry named main. Mutants swapping
   names/function IDs or using first LocalSize must fail this oracle while remaining
   structurally valid where possible.

## Static interfaces, binding aliases and whole-module errors

Use this exact closure witness (runtime arrays have independently checked extents):

```wgsl
@group(0) @binding(0) var<storage, read_write> a: array<u32>;
@group(0) @binding(1) var<storage, read_write> b: array<u32>;
@group(0) @binding(2) var<storage, read_write> unused: array<u32>;
fn touch() -> bool { b[0] = 9u; return true; }
@compute @workgroup_size(1) fn main() {
    a[0] = 1u;
    let skipped = false && touch();
    return;
    a[1] = b[1];
}
@compute @workgroup_size(1) fn other() { unused[0] = 3u; }
@compute @workgroup_size(1) fn shadow() { let b = 4u; a[0] = b; }
```

Expected declaration indices0,1,2; main closure[0,1], other[2], shadow[0]. All three
remain declared. Running main writes only a[0]=1; b and the dead a[1] remain unchanged.
Make touch reachable through two shared helpers and a diamond to prove transitivity,
deduplication and emit-once bodies. Reorder declarations without changing identity.
A constructor/type/local named like a builtin must not become a call/barrier edge.
Include assignment-only roots, compound updates, member/index paths and direct source
input uses in constant-skipped/dead code. Static builtin mask can differ from actual
SPIR-V input-interface mask; synthetic LocalInvocationIndex is not a source builtin.

Binding alias witness uses two entries with separate declarations at identical pairs:
first: storage<f32> group0/binding0 and uniform vec4u group0/binding1;
second: storage<u32> group0/binding0 and read_write storage<u32> group0/binding1.
Use ordinary admitted loads/stores/let syntax and distinct exact expected outputs.
Both entry closures succeed and declared reflection keeps all four records. Add a
helper that makes one entry statically access both group0/binding0 declarations:
module must fail even when the call is false&& or after return, and even if NAME
selects the unaffected sibling. Repeat with identical types/access: still collision.
This is the source-detectable WGSL rule, not external pipeline-layout validation.
Retained `tests/core/wgsl_cross_backend_test.zig:386-402` binds this behavior, but its
phony-assignment spelling may require a separate language owner. A translated/adapted
fixture is not counted as unchanged-source admission; report that distinction.

Module errors must reject NONE, every NAME, AUTO, all output masks and MEASURE:
- Invalid unused helper body, undefined dead reference, invalid constant initializer,
  invalid unused resource/type layout, duplicate entry/module name.
- Unused helper recursion, indirect cycles through skipped calls, calling an entry.
- Invalid later entry workgroup/type/builtin duplicate, sibling capture attempt.
- A later entry with varying control around workgroupBarrier, including source-skipped
  synchronization. A valid selected first entry cannot hide its error.
- Disjoint valid aliases positive control so a blanket module binding check is killed.
Wrong-stage syntax remains explicit UNSUPPORTED, not a new graphics admission.

## All-entry workgroup initialization and uniformity

Exact interleaved fixture:

```wgsl
@group(0) @binding(0) var<storage, read_write> out: array<u32>;
var<workgroup> small_shared: array<u32, 2>;
var<workgroup> wide_shared: array<u32, 7>;
fn read_wide(i: u32) -> u32 { return wide_shared[i]; }
@compute @workgroup_size(2) fn small(
    @builtin(local_invocation_index) lane: u32, @builtin(workgroup_id) group: vec3u) {
    out[group.x * 2u + lane] = small_shared[lane];
    small_shared[lane] = 31u;
}
@compute @workgroup_size(7) fn wide(
    @builtin(workgroup_id) group: vec3u, @builtin(local_invocation_index) lane: u32) {
    out[group.x * 7u + lane] = read_wide(lane);
    wide_shared[lane] = 47u;
}
```

small root closure[0], bytes8; wide root closure[1], bytes28. Two workgroups and repeated
fresh dispatches produce zeros in their active output prefixes, despite hostile nonzero
initial private workgroup memory. Exactly one implicit initialization barrier per active
prologue before user operations. Inspect initialization stores: small must not touch
wide_shared and vice versa; shared helper remains one emitted function.
Freeze schedule permutations that expose missing/late barriers, shared memory leaking
across groups/dispatches, and stale root selection. Add variants using local_invocation_id
instead of local_invocation_index (hidden target input), all five declared builtins in
permuted order, differing 2D/3D dimensions, no roots, only an unused declared root, a
root referenced solely in dead source, and uniform early return. Preserve M11 race and
dynamic-rendezvous negative controls; sequential evaluation is insufficient.

## Source-bound unchanged real sources

Use exact bytes/hashes in contracts/m13_source_inputs.json and preserve the existing consumer caps.
For each, enumerate and execute EVERY exported name in the SAME binary; do not rewrite
entry names/source or count an adapted source as admission:
- rmsnorm.wgsl: main and main_weight_offset, workgroup256, shared_sum1024 bytes;
  uniform group0/binding0 min16, resource bindings1..3, both closures all four.
  One row size4, eps0, input[1,1,1,1], weight[1,1,1,1]: exact outputs main[1,1,1,1]
  versus alternate[2,2,2,2]. This power-of-two case avoids an arbitrary float tolerance;
  add independently frozen finite accuracy cases under inherited sqrt/numeric bounds.
- sample.wgsl: main and sample_single_pass, workgroup256, two roots total2048 bytes;
  all three resource bindings, uniform min16. vocab_size4/logits[-2,7,3,1] yields token1.
  Its entry bodies are identical, so this does not replace the asymmetric name witness.
- matmul_gemv_subgroup.wgsl: main_vec4 and main_multicol, workgroup64, root256 bytes;
  all four bindings, uniform min16. rows1/cols4, matrix[1,2,3,4], vector[1,1,1,1]
  yields output10 exactly under accepted M12 dot. This file uses no subgroup instruction.
  Its entry bodies are identical; wrong-name execution needs the asymmetric fixture.

Allocate minimum/full bindings honestly, separate guards/minimum extent from runtime
array length, use dispatch group counts1 where stated, and initialize unused/canary
regions. Pin all little-endian data bytes and output oracles in the independent seal.
If another admitted-language/pool blocker appears, retain first failure and decide its
owner; never silently increase limits or promise that three files must all pass.
The unchanged 98-source ledger must distinguish parse/validate/emit/spirv-val/execute,
validation-only and capacity/feature rejects. Increased coverage is not whole-compiler
or physical qualification. M12-dependent GEMV cannot freeze against an unaccepted M12.

## ABI, capacity, lifetime and target controls

Run C and C++ consumers against ordinary native static and PIC/shared artifacts,
qualified strict FP flags, all old exports plus exactly the two proposed new exports.
No Zig in generation, compile/link or compiler invocation. Retain original M1–M12
positive/negative numeric/environment/error gates and list every intentional rejection
lift (multiple entries in module/WORDS, disjoint binding pairs, zeroing only a closure).
Old JSON reflects no invented schema. Fixed/job multi-entry projection rejection is
separate from ABI4 WORDS success; ABI4 REFLECTION failure must never publish words.

Selection matrix: NONE/AUTO/NAME × one/multiple entries × first/last/missing/helper/
case mismatch/empty/embedded NUL/NULL-positive names. No default-main or first-entry
fallback. All source validity failures precede successful selection. Validate exact
ABI/struct/record sizes, invalid reserved bits, nullability, alignment and flags;
ABI mismatch must leave ALL caller bytes untouched. Pairwise aliasing is an explicit
caller precondition, not a claimed rejection test. Review that no compiler code adds
restrict assumptions or invents a checked-overlap guarantee; test valid disjoint layouts.

Exact/one-short tests for J, aggregate B, private C, public entries/references/workgroups,
all existing Q/M/R/X/K/L/I/O/D/V/W pools, every output span and names bytes. Shared-helper
multi-entry C counts each distinct entry incidence, not unique module declarations.
Use fixed1/J2 rejection, zero caps, near-limit query overflow and extra workspace bytes
without cap changes. ABI4 derived storage and ABI5 explicit sparse storage must agree
on emitted words/metadata. Record bytes, alignment and pool results from actual queries.
Per-entry inputs spread across entries must exceed five aggregate without rejection;
duplicate kind within one entry must still fail. Source-declared-but-unused resources
remain output-counted but not closure-counted.

MEASURE must match subsequent compile counts/semantics with identical source/selection/
capacities/library; after failed selection, capacity, semantic, target and numeric exits,
reuse one workspace for a different module without reset. Poison/sentinel every output
and spare byte; verify all-or-nothing failure and populated padding initialization.
Overwrite source, selection name and workspace after success and verify owned outputs/
diagnostics. Concurrent calls with distinct workspaces and source-span lifetimes.
No test claims deterministic behavior for invalid accessible-pointer/alias preconditions.

Check SPIR-V1.3 header, all entry name/function/mode bindings, actual ID bound, helper
emission once, per-entry Input-only interfaces, zero workgroup prologues and resource
identity. Retain real65532/65533-formal width controls, type/composite word-width checks,
ID saturation arithmetic and staging W one-short. No public mutation/test-hook exports.
Independently validate with spirv-val Vulkan1.1, then independently execute all entries.
Wrong-but-valid controls: swap names; copy first LocalSize to second; drop closure resource;
map input by sibling ordinal; initialize sibling roots; omit hidden LocalInvocationIndex;
reuse first resource minimum; preserve declared reflection but corrupt selected indices.
A structurally valid mutant must fail an independent semantic/metadata oracle.

Stack acceptance must include the complete accepted M12 recursive SCC and acyclic
paths, normal and PIC qualified builds, all new helper callsites, actual .su records,
checked composed bounds and source identity. Preserve147456/147968/8192/16384 and
212992/196608 ceilings unless separately reviewed BEFORE implementation. The planning
probe is not stack evidence and candidate2's near-full budgets leave no assumed slack.

## Acceptance boundary and remaining gates

Required independent CPU receipts bind exact compiler commit/tree, dirty-file identities
if any, headers, build flags/toolchain, sealed oracle, fixtures, all command lines and
first failure/retry history. Contract review, test seal, implementation review and
independent execution are separate. No contract changes during a failing gate.
Later retained test-wgsl/test-core routes may compare metadata/diagnostics and cache
fixtures; do not run large/native builds while current unrelated gates are active.

Runtime adoption is a distinct gate: TranslationInfo entry metadata, needs_sizes_buf,
buffer/texture obligations, deep clone/free, OOM rollback, cache schema/config/proof
identity and stale/corrupt/truncated decoding, shader→pipeline name/workgroup transfer,
failed replacement preservation. Existing first-workgroup shortcuts must be removed
there, not copied into C. Runtime cache tests are NOT declared passed by this owner.
Physical Vulkan/Metal/D3D12, real package alternate-entry execution, installation and
production default changes remain separately authorized/qualified. CPU instruction
execution does not provide any of those receipts.
