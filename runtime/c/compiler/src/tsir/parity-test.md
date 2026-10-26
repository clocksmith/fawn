# TSIR parity tests (defined before code)

These tests were written from the Zig behavior in parity-mapping.md,
before the C code in `src/`. Each test names the Zig row it checks, the
exact expected value, and what failure means. A test that asserts
`TSIR_ERR_UNSUPPORTED` for a missing row is a passing test about a gap:
it proves the gap fails visibly instead of silently producing output.
It does not close the gap; gaps.md still lists the row as missing.

Conventions: all tests run on Linux with GCC and UBSan
(`-std=c11 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined`).
No test depends on a GPU, a network, the Zig toolchain, or the
`fawn-tree` at run time; fixtures are constructed in C. First error is
preserved: helpers return a `TsirStatus` and never overwrite an earlier
failure with a later success. No test mutates library state through a
public hook; there are none.

## T-SHA: SHA-256 (digest D19)

- T-SHA-1: SHA-256("") =
  `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.
- T-SHA-2: SHA-256("abc") =
  `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`.
- T-SHA-3: SHA-256 of one million "a" bytes =
  `cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0`
  (multi-block path, length padding).
- T-SHA-4: incremental update split at odd boundaries gives the same
  digest as a single update (streaming equivalence).
- Failure meaning: any mismatch invalidates every digest and reference
  hash test below; stop and fix SHA-256 first.

## T-SCHEMA: schema rows S1-S47

- T-SCHEMA-1: scalar byte sizes: f32 4, f16 2, bf16 2, i32 4, u32 4.
- T-SCHEMA-2: every enum name table round-trips: for each enum value,
  `tsir_*_name` returns the exact Zig snake_case tag, and an
  out-of-range value returns NULL (never a neighbouring name).
- T-SCHEMA-3: exactness well-formedness: algorithm_exact with an
  invariant passes; algorithm_exact with a tolerance metric fails;
  tolerance_bounded with epsilon 0 and empty metric fails (metric
  required); bit_exact_solo with invariants or epsilon fails.
- T-SCHEMA-4: contract version constant is 1; a semantic built with
  contract_version 0 is rejected by digest canonicalization as invalid.

## T-DIGEST: canonical JSON and digests (D1-D18)

Fixtures are hand-built minimal semantics/realizations so the canonical
bytes can be asserted exactly, not just hashed opaquely.

- T-DIGEST-1: empty semantic
  (`{"contractVersion":1,"frontendVersion":"","functions":[],"rejections":[]}`)
  canonicalizes to exactly that string, and its SHA-256 equals the
  digest returned by `tsir_digests_compute` for the semantic slot.
- T-DIGEST-2: a one-function semantic with one axis, one f32 binding
  `[4]`, one sum reduction, and no body payload canonicalizes to the
  exact string built by hand from the D3-D12 field orders (the test
  contains the full expected string; any key-order or spelling drift
  fails the string comparison before any hash comparison).
- T-DIGEST-3: string escaping: a binding name containing `"`, `\`,
  newline, tab, and 0x01 escapes as `\"`, `\\`, `\n`, `\t`, `\u0001`
  in canonical output, matching the Zig escape set.
- T-DIGEST-4: optional fields: absent axis/shards/fabric_color/
  chunk_bytes serialize as `null`, present ones as numbers; digests
  are 64 lowercase hex chars.
- T-DIGEST-5: NaN tolerance_epsilon and Inf literal_f64 are rejected
  with TSIR_ERR_INVALID, never serialized.
- T-DIGEST-6: manifest lowering entry canonicalizes in D17 order and
  its digest equals SHA-256 of those bytes.
- T-DIGEST-7: changing one byte of a binding name changes the semantic
  digest; changing only emitter_params_json changes the realization
  digest but not the semantic digest (split-digest separation).
- T-DIGEST-8: digest emits rms_norm and drops other body payloads:
  a body with op rms_norm plus a (struct-level) attention payload
  canonicalizes with only `rmsNorm` present, matching the Zig digest
  restriction (D5). The C struct carries the other payloads, the digest
  does not emit them, exactly as Zig does.
- Known risk (not hidden): Zig formats f64 with `{d}` (shortest
  round-trip). C uses a shortest-round-trip writer; T-DIGEST-9 checks
  1.5 and 0.000001 as exact canonical strings, and checks that
  changing only the epsilon changes the digest. Arbitrary f64 values
  (other exponents, negative zero) are a documented divergence risk
  in gaps.md, not covered by a passing test.

## T-SCALAR: reference scalar rows R7-R11

- T-SCALAR-1: f32 round-trip through read/write at several indices,
  including -0.0, subnormals, Inf, and quiet NaN bit patterns.
- T-SCALAR-2: f16 conversions: 1.0 -> 0x3c00, -1.0 -> 0xbc00, 0.5 ->
  0x3800, 65504.0 -> 0x7bff, 65520.0 rounds to Inf (0x7c00),
  round-to-nearest-even tie: 1.00048828125 (midpoint between 1.0 and
  next f16) -> 0x3c00 (even mantissa), and the next value up rounds up.
- T-SCALAR-3: bf16 RNE: 1.0 -> 0x3f80; tie case 1.00390625 (0x3f808000)
  -> 0x3f80 (even); 1.01171875 (0x3f810000) -> 0x3f81; signaling/quiet
  NaN input keeps NaN with the quiet bit set, never becomes Inf.
- T-SCALAR-4: bf16 read is the high 16 bits: bits 0x3f800000 read as
  bf16 0x3f80 give 1.0.
- T-SCALAR-5: expected bytes: shape [2,3] f32 -> 24; shape [0,5] -> 0;
  shape [2^32, 2^32] f32 -> TSIR_ERR_OVERFLOW, never a wrapped value.
- T-SCALAR-6: reduction identities: sum 0, product 1, min +Inf, max
  -Inf; softmax_stable identity -> TSIR_ERR_INVALID (documented
  divergence from the Zig trap; no value is produced either way).
- T-SCALAR-7: combine: min/max propagate NaN (min(NaN, 1) is NaN under
  the propagate contract as implemented by Zig @min/@max semantics in
  the scalar helper); sum/product are plain IEEE ops.
- T-SCALAR-8: integer read/write: i32/u32 element access returns
  TSIR_ERR_UNSUPPORTED, never a reinterpreted float.
- T-SCALAR-9: read-only binding lookup: inputs are indexed by ascending
  binding index of read-only bindings only; a read-write binding index
  returns "not found"; an out-of-range index returns "not found".

## T-PLANNER: planner rows P1-P13

Target descriptor fixture A (generous): pe_working_memory_bytes 65536,
fabric_color_count 4, max_collective_group_size 32, numerical modes
{f32, f16, bf16}, collectives {subgroup_add/bit_exact_solo,
fabric_allreduce/algorithm_exact}, policy host_copied.
Fixture B (tight): pe budget 64, colors 0, max group 1, modes {f32},
collectives {}, policy reject.

- T-PLANNER-1: tile choice: upper_bound "100" -> 64; "63" -> 32;
  "1" -> 1; "0" -> 1; "abc" -> 1; "64" -> 64; "65" -> 64.
- T-PLANNER-2: PE grid: fixture A -> width 8 (cap), height 1;
  fixture B -> 1x1 (zero colors).
- T-PLANNER-3: residency replicated: binding [16] f32 (64 bytes)
  under fixture B (budget 64) -> pe_replicated.
- T-PLANNER-4: residency sliced: binding [64] f32 (256 bytes) under a
  budget-64, width-4 target -> pe_sliced on axis 0 with shards 4
  (256/4 = 64 fits).
- T-PLANNER-5: residency rejection: binding [64] f32 under fixture B
  (budget 64, no streaming, policy reject) with no slice that fits
  (shape [64] sliced by 1 -> still 256) -> exactly one rejection,
  reason tsir_pe_budget_exhausted, path `functions[0].bindings[0]`,
  detail exactly "binding footprint exceeds per-PE budget without legal
  slice or stream", and no residency decision for that binding.
- T-PLANNER-6: residency streamed: runtime-sized binding (shape [0])
  under a streaming loader (fabric_streaming true, max chunk 4096,
  colors 4, policy fabric_streamed_with_loader) -> fabric_streamed,
  color 0, chunk 4096; with max chunk 0 -> chunk = pe budget.
- T-PLANNER-7: residency host-copied: runtime-sized binding under
  policy host_copied -> host_copied regardless of loader streaming.
- T-PLANNER-8: reduction synthesis: associative_allowed with f32
  accumulation under fixture A -> one linear node; strict_ordered ->
  no node; bf16 accumulation under fixture B (modes {f32}) -> one
  tsir_target_unfit rejection, no node.
- T-PLANNER-9: collective synthesis: subgroup_add f32 bit_exact_solo
  under fixture A -> linear node, group_size = min(32, tile); fabric
  kind consumes colors in source order (first fabric node color 0,
  second color 1); fifth fabric node under colors 4 -> rejection
  tsir_collective_not_representable, "target fabric color budget
  exhausted"; a collective whose dtype is not native -> rejection
  tsir_target_unfit even before the exactness check.
- T-PLANNER-10: exactness satisfaction: capability bit_exact_solo
  satisfies algorithm_exact and tolerance_bounded requirements;
  capability algorithm_exact does not satisfy bit_exact_solo.
- T-PLANNER-11: default emitter params: a plan built with default
  options carries exactly `{"planner":"tsir.first_fit_v1"}`.
- T-PLANNER-12: first error preserved: a plan with two failing
  bindings reports both rejections in binding order (rejections are
  accumulated, not short-circuited).

## T-REFERENCE: interpreter rows R1-R21

All reference tests build the semantic in C, run
`tsir_reference_run`, and compare output bytes and the reference hash
(SHA-256 over concatenated outputs in declared order).

- T-REF-1: empty kernel: 1 function, no bindings/reductions/
  collectives, no inputs -> zero outputs, hash =
  e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855.
- T-REF-2: rejection first: same empty kernel but with one semantic
  rejection -> TSIR_ERR_REJECTED, no outputs, even though the shape
  would otherwise match; same for a realization rejection.
- T-REF-3: rank-1 sum: input [1,2,3,4] f32, strict_ordered -> 10.0;
  product -> 24.0; min -> 1.0; max -> 4.0; empty input [0] with
  identity: sum -> 0.0, product -> 1.0.
- T-REF-4: rank-1 binomial vs linear differ on purpose: input
  [1.0, 1e8, -1e8, 1.0] f32 sum. Linear left-fold: 1.0 + 1e8 rounds to
  1e8 (ulp at 1e8 is 8), minus 1e8 gives 0.0, plus 1.0 gives 1.0.
  Binomial pairwise: (1.0 + 1e8) = 1e8, (-1e8 + 1.0) = -1e8, sum =
  0.0. The test asserts linear = 1.0 and binomial = 0.0 exactly, so
  the two fold orders are provably not unified. (For n = 3 the Zig
  pairwise rule coincides with left-fold; the test uses n = 4, where
  it does not.)
- T-REF-5: rank-2 sum over axis 1: [[1,2],[3,4]] -> [3,7]; over
  axis 0 -> [4,6]; output shape mismatch -> TSIR_ERR_UNSUPPORTED
  (fall-through equivalent), not a wrong-shaped output.
- T-REF-6: rank-3 sum: shape [2,2,2] values 1..8, axis 1 -> the
  documented row-major result; axis out of range -> unsupported.
- T-REF-7: rank-4 generic: shape [2,1,2,1] reduced on axis 2, linear
  fold, matches a hand-computed result; binomial at rank 4 ->
  TSIR_ERR_UNSUPPORTED.
- T-REF-8: associative_allowed without a matching realization node ->
  TSIR_ERR_UNSUPPORTED; with a linear node -> linear result; ring
  node -> same bits as linear.
- T-REF-9: dtype paths: f16 input [1,2,3] sum -> 6.0 written as f16;
  bf16 input sum -> bf16 output with RNE rounding; integer input
  binding -> TSIR_ERR_UNSUPPORTED.
- T-REF-10: GEMV: W [[1,2],[3,4]], x [5,6] -> [17, 39] f32;
  K == 0 -> zeros; vector/matrix role order swapped in binding
  indices still works (inputs follow read-only binding order);
  non-f32 accumulation -> unsupported.
- T-REF-11: RMSNorm literal epsilon: x [3,4], scale [1,1], eps 0 ->
  mean_sq = 12.5, inv_rms = 1/sqrt(12.5), outputs x*inv_rms; with
  scale [2,2] outputs double that; hidden 0 -> zero-length output.
- T-REF-12: RMSNorm uniform epsilon: epsilon read from a uniform
  binding at a stated byte offset (path `uniform:<name>.eps`), value
  1e-5, matches the literal-epsilon computation with eps 1e-5;
  wrong field name (not `eps`) -> unsupported; NaN epsilon ->
  unsupported.
- T-REF-13: gather: indices [2,0] u32, table [[10,11],[20,21],
  [30,31]] -> [[30,31],[10,11]]; index == vocab -> unsupported
  (fall-through equivalent); indices elem not u32 -> unsupported.
- T-REF-14: extended-op shapes are visibly unsupported: residual_add,
  gated, attention_scores, l2_normalize, conv1d_depthwise,
  linear_attention, and identity-shaped semantics all return
  TSIR_ERR_UNSUPPORTED from `tsir_reference_run`, with no output
  buffers allocated. (These tests pass by asserting the gap; gaps.md
  still lists R22-R28 as missing.)
- T-REF-15: NaN propagation: rank-1 sum over [NaN, 1.0] with
  propagate -> NaN output bits preserved as quiet NaN through the
  f32 path (test asserts isnan, not a specific payload, because Zig
  @min/@max NaN payload behavior is not pinned beyond propagation).

## T-EMIT: emitter rows E1-E17

- T-EMIT-1: metadata writers byte-exact: residency/tiles/collectives/
  reductions blocks for a fixed fixture match the expected strings in
  the test file character for character, including `none` for absent
  optionals and the trailing blank line of each block.
- T-EMIT-2: skeleton header: DXIL/MSL/SPIR-V realization-only outputs
  start with the exact version line, target name line, descriptor hash
  line (64 hex), semantic_index, pe_grid lines, quoted emitter params,
  blank line, then metadata blocks, then the backend body_comment
  line. The test asserts the full prefix, not a substring.
- T-EMIT-3: skeleton rejection gate: a realization with any rejection
  -> TSIR_ERR_REJECTED from every skeleton emitter, with no partial
  output published (the output buffer stays empty on failure).
- T-EMIT-4: skeleton index gate: function_index out of range ->
  TSIR_ERR_RANGE; semantic-aware skeleton with a mismatched
  target_descriptor_hash -> TSIR_ERR_INVALID.
- T-EMIT-5: semantic-aware emission without a kernel body ->
  TSIR_ERR_UNSUPPORTED for every backend (E13 gap, asserted visibly).
- T-EMIT-6: dtype routing: cslElemName/wgslElemName tables (bf16 ->
  "f16" in WGSL spelling); supported compute elems are f32/f16 only;
  writeWgslF16Enable emits `enable f16;\n\n` for f16 and nothing for
  f32.
- T-EMIT-7: kernel-context helpers: bindingForRole finds the first
  matching role and reports missing roles; bindingForIndex out of
  range is an error; writeCslSqrtNr f32 form and upcast form match
  the expected CSL text exactly; buffer array/pointer/export lines
  match exactly.
- T-EMIT-8: quoted writer: escapes backslash, quote, newline, carriage
  return, tab; leaves other bytes unchanged (metadata quoting is not
  digest escaping).

## T-FRONTEND: frontend rows F2, F8

- T-FRONTEND-1: IR scalar mapping: f32, f16, i32, u32, abstract_int
  -> i32, abstract_float -> f32, bool -> u32, void -> u32.
- T-FRONTEND-2: family-hint pure classifier: reductions present with
  the fused_gemv shape facts -> fused_gemv; reductions present
  without them -> reduction; axes present without reductions ->
  elementwise; neither -> unknown.
- T-FRONTEND-3: full frontend lowering returns TSIR_ERR_UNSUPPORTED
  (F1 gap, asserted visibly).

## What these tests deliberately do not cover

- No Zig cross-check: this VM has no Zig toolchain requirement stated,
  and no Zig binary is invoked. Byte-exactness against Zig is claimed
  only where the Zig rule is fully stated in the source read during
  this run (canonical field orders, fold orders, RNE rules, skeleton
  text). The f64 shortest-round-trip rule and the uniform-path
  splitter are flagged in gaps.md as the two places where a Zig
  cross-check is still needed before any manifest-comparison claim.
- No GPU execution, no DXIL/CSL/MSL/SPIR-V/WGSL compilation of emitted
  text (the skeleton emitters emit contract text, not compilable
  modules, exactly as the Zig skeleton emitters do).
- No bootstrap tools (T1, T2): no JSON layer is built here, so no tool
  test exists. A future tool test must drive the C library through a
  real JSON parser, not a hand-rolled one.

## T-FRONTEND-IR: WGSL IR lowering (F1, F4-F9, new in this run)

The C IR model (`tsir_wgsl_ir.h`) mirrors the Zig IR node kinds used
by the frontend only: types (scalar/vector/matrix/array/ref/struct/
atomic/other), struct definitions, globals (class, address space,
access, binding, override id), params (builtin global_invocation_id
only), locals, expressions (all Zig `Expr` variants), statements
(block/local_decl/expr/assign/return/if/loop/switch), and functions.
Types the frontend never reads (sampler/texture kinds) collapse to
`other`; that collapse is documented, not hidden: a global whose type
is `other` gets the tsir_binding_type_unlowerable rejection exactly
as Zig's `else => return null` branch does.

- T-FIR-1: binding extraction: only globals with a @group/@binding
  that some expression in the function references (global_ref) are
  kept, in module-global order; an unbound or unreferenced global is
  dropped; a referenced storage-struct global (non-uniform struct)
  produces one tsir_binding_type_unlowerable rejection at
  `functions[i].bindings[group:binding]` and is dropped; a uniform
  struct global becomes a u32 binding whose shape is
  byte_size/4 (std430 layout via tsir_wgsl_ir layout utils).
- T-FIR-2: axes: `for (var i = 0u; i < 4u; i += 1u)` gives axis
  (i, "0", "4", "1"); mirror form `4u > i` gives the same; `i <= 3u`
  gives upper "4"; symbolic `i < K` where K is an override global
  gives "override:K" (or "override@id:N" when the override carries
  an id); `i < u.count` where u is a uniform var gives
  "uniform:u.count"; a dispatch local `let t = gid.y` followed by
  `if (t >= u.num_tokens) { return; }` gives a dispatch axis with
  upper "uniform:u.num_tokens" (guard scan past skip-safe siblings);
  a decreasing loop emits no axis and a tsir_source_not_affine
  rejection.
- T-FIR-3: reductions: `acc += x` inside an inner for loop gives a
  sum region on the inner axis with contract (f32, strict_ordered,
  propagate) and target_binding resolved by the post-loop writeback
  scan (direct `out[i] = acc` and chained `let m = acc / n; ...`
  epilogue aliasing); expanded `acc = acc * x` gives product;
  `acc = max(acc, x)` / swapped gives max; no writeback gives
  target 0 plus a tsir_dependence_unanalyzable rejection; a
  non-scalar accumulator gives f32 plus the same reason.
- T-FIR-4: collectives: a builtin `subgroupAdd` call inside a loop
  gives one subgroup_add node with axis = innermost enclosing
  for-loop axis, exactness algorithm_exact with invariants
  [reduction_order, associativity_grouping], dtype from the call
  return type; outside any loop axis is -1; `workgroupBarrier`
  gives dtype u32 with no dtype rejection; a non-scalar return type
  gives dtype u32 plus a tsir_collective_not_representable
  rejection at `functions[i].collectives[j]`.
- T-FIR-5: rejections: a while loop at root position k gives
  tsir_dependence_unanalyzable at `functions[i].body[k]` with detail
  "while loop"; a bare loop gives "unstructured loop"; nesting
  appends `.body[...]` segments and `.then`/`.else` for if branches.
- T-FIR-6: family hint from IR (no caller-supplied booleans): a
  two-axis, one-sum-reduction function whose loaded index expression
  mentions both axis locals gives fused_gemv; the same shape with
  split accesses gives reduction; axes with an index whose index
  field contains a load of a buffer element (indirect, depth <= 8
  local-alias chain) gives gather; otherwise elementwise/unknown.
- T-FIR-7: body inference: fused_gemv hint with >=2 axes, >=3
  bindings, >=1 reduction gives roles (matrix 0, vector 1,
  output 2) and axis roles (output 0, reduction = reduction axis);
  gather hint gives (indices 0, table 1, output 2) and (token 0,
  hidden 1); bindings named input/weight|scale/output with equal
  first/reduction upper bounds and a uniform epsilon binding named
  `u` with an f32 `eps` field give rms_norm with epsilon
  uniform_field, path built from the hidden bound (or
  "uniform:u.eps" by default), binding index of `u`, and byte
  offset from std430 layout; otherwise body op is unknown.
- T-FIR-8: full lowering end to end: one function, source_digest
  carried per function, contract_version 1, frontend_version as
  supplied; a second function touching a disjoint binding set gets
  only its own bindings (per-function filtering, no digest
  collision). Lowering never fabricates axes, reductions, or bodies
  for shapes it does not recognize; those stay absent with the
  typed rejection where Zig emits one.
- Known limit: switch-case bodies are not descended by the axis /
  rejection walkers (Zig's walkers do not descend switch either,
  except the collective walker which walks only the switch
  expression); `loop` continuing blocks are walked for axes and
  reductions as in Zig. IR trees deeper than the walker's guard
  return the conservative no-evidence result rather than recursing
  unboundedly.

## T-EXTENDED: extended ops (R22-R28, new in this run)

All extended tests build the semantic in C, run
`tsir_reference_run`, and compare output bytes exactly, except
transcendental cases (see T-TRANS below).

- T-EXT-1: identity: exactly one read-only and one read-write
  binding, equal shape/elem, one input of the exact expected byte
  length -> byte copy output; wrong input length -> unsupported.
- T-EXT-2: residual_add: roles summand_a/summand_b/output, one
  hidden axis, all f32, 1-D equal shapes -> elementwise f32 sums
  (IEEE, including Inf/NaN passthrough by plain addition).
- T-EXT-3: gated, exact cases: gate 0.0 gives gelu 0.0, silu 0.0,
  sigmoid 0.5 times the input value (exp(0) = 1 and tanh(0) = 0 are
  exact, so these outputs are bit-exact, not tolerance cases);
  gate values clamped at +/-15 are exercised for saturation
  behavior (output within the clamped form, checked by value, not
  by bit equality against Zig).
- T-EXT-4: attention_scores: two_pass_stable, literal scale, no
  softcap, single-Q, head_dim 2, kv_len 2 with Q/K/V chosen so
  scores are 0 (exp(0) = 1 exact) -> uniform weights 0.5 and the
  exact mean of V rows; streaming mode, softcap, or
  query_seq_len > 1 -> unsupported; sum_exp == 0 -> unsupported.
- T-EXT-5: l2_normalize: input [3,4], eps 0 -> [0.6, 0.8] within
  1e-6 (sqrt path, not a bit claim); hidden 0 or missing body
  payload -> unsupported.
- T-EXT-6: conv1d_depthwise: causal left pad, kernel_size 2,
  channels 1, with and without bias, hand-computed outputs exact
  for f32 (addition/multiplication only, no transcendentals);
  has_bias without a bias role -> unsupported.
- T-EXT-7: linear_attention: the interpreter dispatch preserves
  Zig's fall-through (see T-EXT-8), while the state-update math
  itself is tested directly through
  `tsir_linear_attention_step` with a_log = 0.5, alpha =
  1 - exp(-0.5), decay = 1 - alpha: a zero prior state, q/k/v/g
  fixture gives the hand-computed new state and output (sigmoid
  term checked at g = 0, where sigmoid = 0.5 exactly).
- T-EXT-8: linear_attention interpreter reachability (documented
  Zig behavior, not a C shortcut): Zig's tryLinearAttention reads
  the read-write state binding through
  inputBytesForReadOnlyBinding, which returns null for any
  read-write binding, so the Zig path always falls through for its
  own declared shape. C preserves that: a linear_attention semantic
  through `tsir_reference_run` returns TSIR_ERR_UNSUPPORTED, and
  gaps.md records the Zig-side dead path instead of silently
  redefining the input convention. The math is still implemented
  and tested (T-EXT-7).

## T-TRANS: transcendentals (R29, new in this run)

Zig's interpreter header declares exp/log/sin/cos/tan/tanh/rsqrt/
recip as sollya-bounded minimax polynomials with ULP bounds
(1,1,1,1,2,2,1,1) and states libm is NOT used, but the Zig source
in this tree calls std.math.exp/tanh/sqrt, whose exact bit output
cannot be established from this VM (no Zig toolchain, no Zig
binary). C therefore evaluates exp/tanh/sqrt in double precision
and rounds to f32 (correctly rounded to within 1 ULP of the true
value for the tested range). That is NOT a claim of bit equality
with Zig: any test that needs a transcendental value asserts
either an exactly-representable case (exp(0) = 1, tanh(0) = 0,
sigmoid(0) = 0.5) bit-exactly, or a non-trivial value within a
stated absolute tolerance, and gaps.md records bit parity for
every transcendental-bearing op as unestablished pending a Zig
cross-check. No test asserts a full reference hash for a gated,
attention, or linear_attention case as "Zig-equal".

## T-KERNELBODY: kernel bodies (E8, E12, E13, E20, E21, E23, new)

- T-KB-1: WGSL fused_gemv: exact full text for a fixture with
  groups/bindings (0,0)/(0,1)/(0,2), all f32: the three
  @group/@binding lines, `@compute @workgroup_size(1, 1, 1)`,
  `fn main`, row/rows/cols guard, acc loop, and output store, in
  Zig's order. Non-f32 bindings -> unsupported; missing role ->
  unsupported.
- T-KB-2: WGSL rms_norm: literal epsilon 0.00001 emits the
  `let hidden_size = arrayLength(&tsir_output);` form and
  `inverseSqrt(mean_sq + 0.00001)`; uniform epsilon with byte
  offset 4 emits the TsirRmsNormUniforms struct and the
  `tsir_dims` uniform line; offset != 4 -> invalid (Zig
  InvalidBodyContract).
- T-KB-3: WGSL gather: exact full text for the fixture, including
  the out-of-range row -> 0.0 branch.
- T-KB-4: MSL/DXIL/SPIR-V fused_gemv, rms_norm, gather: exact full
  text for the f32 fixtures (MSL metal_stdlib header and
  function constants, DXIL register/cbuffer form, SPIR-V GLSL
  form), matching Zig's emitted lines in order; rms_norm uniform
  epsilon emits the `tsir_epsilon` constant form.
- T-KB-5: semantic-aware emission: `tsir_emit_semantic` with a
  supported body now returns header + metadata + body (no longer
  unsupported); realization-only skeleton output is unchanged;
  rejection gate, index gate, and descriptor-hash gate still fire
  before any body text; an unsupported op (unknown,
  attention_scores on a non-CSL backend) returns unsupported with
  no partial output published.
- T-KB-6: CSL backend stays quarantined (scope correction
  2026-10-26, overriding the earlier ALL reading): the CSL
  backend, emit_csl.zig, and the CSL-only kernel-body files
  (attention, conv1d, gated, kv, l2_normalize, linear_attention
  CSL bodies) are NOT built in this run and no CSL text is
  emitted or asserted. `tsir_emit_kernel_body` with the CSL
  backend returns TSIR_ERR_UNSUPPORTED. The non-CSL backends
  (webgpu, msl, dxil, spir_v) reject attention_scores and
  every other non-gemv/rms/gather op with unsupported, exactly
  as Zig's non-CSL dispatch does. Any CSL text found in a test
  would be a scope failure, not a pass.
- T-KB-7: emitterCodeDigest is still NOT offered (see gaps.md
  G-EMITTER-DIGEST): no test asserts a digest value for any
  emitter, and `tsir_emit` exposes no digest function, so a
  manifest lowering entry cannot be silently compared across
  implementations.

## T-BOOTSTRAP: bootstrap tools (T1, T2, new in this run)

The JSON layer reuses the repo's bounded JSON reader
(`runtime/c/runtime-cli/src/json.{h,c}` at fawn-tree head
43040741d, copied into `src/tsir_json.{h,c}` with provenance
noted there); no general JSON parser was hand-rolled for this
directory. The JSON writer for status/manifest output is a small
escaping writer (backslash, quote, newline, carriage return, tab
only, matching Zig's writeJsonStringContents), not a parser.

- T-BOOT-1: manifest inputs constants: kernels fused_gemv
  (algorithm_exact, invariants reduction_order/accum_dtype),
  rms_norm (tolerance_bounded, max_abs, 0.000001), gather
  (bit_exact_solo); frontend version
  "frontend-bootstrap-pipeline-v1"; compiler version
  "doe-tsir-bootstrap-2026-04-24"; backends webgpu-generic, msl,
  spir-v, plus wse3 only when the caller asks for CSL.
- T-BOOT-2: manifest entry writer: one entry serializes in
  Zig's writeEntryInput field order (backend, compilerVersion,
  emitterDigest, exactnessClass, algorithmExactInvariants,
  toleranceMetric, toleranceEpsilon, frontendVersion, kernelRef,
  rejectionReasons, targetDescriptorCorrectnessHash,
  tsirRealizationDigest, tsirSemanticDigest) with digests as
  64-char lowercase hex supplied by the caller; the writer does
  not invent digests.
- T-BOOT-3: oracle JSON: a fused_gemv document in Zig's shape
  (kernel name, inputs as an object of buffers with elem/shape/
  values, and the array form) parses, builds the Zig oracle
  semantic (W/x/y, axes i/k, sum reduction), runs the reference
  interpreter, and reports status "pass" with the reference hash
  hex; a mismatched kernel name, a missing buffer, or malformed
  JSON reports "not_implemented" with a null hash, never a crash;
  inputs larger than 16 MiB are rejected; bytesHex inputs decode
  exactly; f16 values round through the f16 converter and bf16
  through RNE, matching Zig's writeValue.
- T-BOOT-4: oracle rms_norm and gather documents run through the
  same path with Zig's buffer names (input/weight/u, indices/
  table) and shapes; the rms_norm uniform buffer supplies epsilon
  at byte offset 4 (second u32 word) as in Zig's oracle.
- What is still missing (gaps.md): the WGSL pipeline behind
  manifest inputs (parser/sema/ir_builder) and the descriptor-hash
  computation, so manifest entries cannot be produced end to end
  from WGSL files in this directory; T1 stays partial, not done.
