# TSIR gaps: unimplemented or unverified work

This file lists everything NOT done, so the coordinator never has to
guess from the passing tests. A test that asserts
`TSIR_ERR_UNSUPPORTED` for a row below is evidence that the gap fails
visibly; it does not close the gap.

## Count correction (verified, not assumed)

The task brief said 38 Zig files in `tsir/` and 40 total. The tree at
head `43040741d` has 36 files in `tsir/` (9,923 lines) and 2 in
`tools/` (751 lines): 38 files, 10,674 lines total. Line counts match
the brief exactly; file counts were off by two, and the table below
uses the verified 36 + 2.

## Per-file status (Zig file -> C status)

Done means: implemented in `src/`, covered by a passing test, and the
behavior matches the Zig rule as read during this run. Partial means
the named subset only. Missing means the C entry point, where one
exists, returns `TSIR_ERR_UNSUPPORTED` visibly. Quarantined means CSL
code excluded from default parity by the TSIR `CATSCAN.md` and by the
coordinator's clarification that CSL can be ignored; mapped, not built.

| Zig file (lines) | Status | C location / note |
| --- | --- | --- |
| collective_synthesis.zig (144) | done | `tsir_planner.c` collective synthesis |
| digest.zig (999) | done, with one unverified corner (see G-F64) | `tsir_digest.c` (923 lines), `tsir_sha256.c` |
| emit_csl.zig (237) | quarantined, not built | CSL emitter; see mapping E18 |
| emit_csl_f16_pack.zig (48) | done (text helpers only) | `tsir_emit.c` |
| emit_dtype_routing.zig (73) | done | `tsir_emit.c` |
| emit_dxil.zig (71) | partial: skeleton done, semantic body missing | `tsir_emit.c`, spec `tsir_spec_dxil` |
| emit_kernel_body.zig (896) | missing | `tsir_emit_kernel_body` returns TSIR_ERR_UNSUPPORTED for every op. The WGSL fused_gemv template was sighted during this run (f32-only bindings, one workgroup_size(1,1,1) compute body) and is the concrete next implementation step, along with the MSL/DXIL/SPIR-V counterparts for fused_gemv, rms_norm, gather. |
| emit_kernel_body_attention.zig (443) | quarantined, not built | CSL-only attention body |
| emit_kernel_body_conv1d.zig (110) | quarantined, not built | CSL-only |
| emit_kernel_body_gated.zig (115) | quarantined, not built | CSL-only |
| emit_kernel_body_kv.zig (245) | quarantined, not built | CSL-only |
| emit_kernel_body_l2_normalize.zig (73) | quarantined, not built | CSL-only |
| emit_kernel_body_linear_attention.zig (169) | quarantined, not built | CSL-only |
| emit_kernel_context.zig (93) | done | `tsir_emit.c` (config, role/index helpers, CSL text helpers) |
| emit_msl.zig (71) | partial: skeleton done, semantic body missing | `tsir_emit.c`, spec `tsir_spec_msl` |
| emit_semantic_metadata.zig (91) | done | `tsir_emit.c` |
| emit_spir_v.zig (71) | partial: skeleton done, semantic body missing | `tsir_emit.c`, spec `tsir_spec_spir_v` |
| emit_text_skeleton.zig (132) | partial: header, metadata, realization-only path done; semantic-aware body step missing | `tsir_emit.c` |
| emit_webgpu.zig (141) | partial: realization-only skeleton done; semantic WGSL bodies missing | `tsir_emit.c` (WebGPU uses the same skeleton path for realization-only calls) |
| family_hint.zig (183) | partial: pure classifier done; IR walking (indexed-access coverage, indirect-access detection) missing | `tsir_frontend.c` `tsir_family_hint_classify`; evidence booleans are caller-supplied and are never claimed to be IR-derived |
| frontend.zig (885) | missing | `tsir_frontend_lower` returns TSIR_ERR_UNSUPPORTED. Blocked on a C port of the WGSL IR module (`../wgsl/ir/ir.zig`, `ir_query`, `layout_utils`), which is outside the TSIR directory. |
| frontend_body.zig (178) | missing | same blocker as frontend.zig |
| frontend_collectives.zig (246) | missing | same blocker |
| frontend_expr.zig (637) | missing | same blocker |
| frontend_rejections.zig (122) | missing | same blocker |
| frontend_types.zig (27) | done | `tsir_frontend.c` scalar mapping |
| mod.zig (57) | partial: public surface is spread across the C headers (no single re-export module); CONTRACT_VERSION done | no separate `tsir_mod` file; a coordinator decision whether a single umbrella header is wanted |
| planner.zig (295) | done | `tsir_planner.c` |
| planner_support.zig (47) | done | `tsir_planner.c` |
| reference_extended_ops.zig (841) | missing | identity, residual_add, gated (gelu/silu/sigmoid), attention_scores, l2_normalize, conv1d_depthwise, linear_attention all return TSIR_ERR_UNSUPPORTED. Deliberately not approximated with libm: the Zig oracle's transcendental behavior is declared (minimax ULP bounds), and a libm approximation would pass smoke tests while breaking bit parity. |
| reference_inputs.zig (26) | done | `tsir_scalar.c` |
| reference_interpreter.zig (762) | partial: dispatch, empty kernel, GEMV, RMSNorm, gather done; extended-op dispatch missing; transcendental table metadata only | `tsir_reference.c`, `tsir_reference_bodies.c` |
| reference_reduction.zig (497) | done | `tsir_reference.c` generic rank path (ranks 1, 2, 3, 4+) |
| reference_scalar.zig (113) | done | `tsir_scalar.c` |
| reference_types.zig (18) | done | `tsir_reference.h` result type |
| schema.zig (767) | done | `tsir_schema.h`, `tsir_schema.c` |
| tools/tsir_bootstrap_manifest_inputs.zig (220) | missing | no C tool built; constants recorded in mapping T1 |
| tools/tsir_bootstrap_oracle.zig (531) | missing | no C tool built; the C library already covers its three kernel shapes, so a future tool is a JSON/argument layer, not a new interpreter. No JSON parser is hand-rolled here. |

Totals: done 14 files, partial 8 files, missing 9 files,
quarantined (not built by design) 7 files. 14 + 8 + 9 + 7 = 38.

## Open items that no code in this directory resolves

- G-REGISTRY: the 103-row capability registry and the 266-name native
  API inventory were not found in this tree. No registry-row mapping
  is claimed. The coordinator must supply the inventory file (reported
  to live on the user's Mac) before that mapping can be written.
- G-DESCRIPTOR-HASH: `targets.descriptorHash` lives outside TSIR.
  The C planner leaves `target_descriptor_hash` zeroed, and C
  emitters take the hash as a caller-supplied input and compare it.
  A plan must not be digest-compared or emitted as "matching Zig"
  until the hash function is ported or supplied.
- G-F64: Zig f64 `{d}` formatting is verified only for the plain
  cases in the tests (0.5-style values, 1.5, 0.000001, integers).
  Scientific-notation exponents, negative zero, and other exotic
  values need a Zig cross-check before any manifest digest is
  compared byte-for-byte against Zig output. The C writer is
  deterministic and round-trips, but determinism is not Zig equality.
- G-EMITTER-DIGEST: Zig `emitterCodeDigest` hashes Zig source text.
  No C equivalent is offered, because hashing C sources would give a
  different value and hashing Zig sources from C would require
  carrying Zig bytes as data. Coordinator decision needed before
  manifest lowering entries can be compared across implementations.
- G-SOFTMAX-TRAP: Zig traps on softmax_stable reaching the scalar
  fold helpers; C returns TSIR_ERR_INVALID. Neither produces a
  value, but the observable behavior differs (trap vs error code).
  If the coordinator needs trap-identical behavior, say so; the C
  choice was made because a library must not trap on bad input.
- G-STRINGS: the C API uses NUL-terminated strings, so TSIR names
  containing a NUL byte cannot be represented. Zig slices can contain
  them. No test covers that case because the API cannot express it.
- G-MINMAX-NAN: C min/max combine propagates NaN, matching the Zig
  scalar helper's documented propagate contract. If Zig's `@min`/`@max`
  on the coordinator's toolchain instead follow C `fmin`/`fmax`
  (NaN-dropping) semantics, the rank-1 min/max NaN tests would need
  revisiting. The tests assert propagation, per the Zig comment.
- G-GEMV-NANINF: Zig GEMV code does not check `nan_inf`, although
  its docstring summary mentions propagate. C follows the code, not
  the docstring. If the docstring is the intended contract, GEMV
  needs a nan_inf check added in both implementations' tests.
- G-NO-ZIG-CROSSCHECK: no Zig toolchain was invoked and no output
  byte string in this directory was compared against a Zig binary's
  output. All "byte-exact" claims rest on reading the Zig source
  during this run. A Zig cross-check (canonical strings, digests,
  reference hashes, skeleton text) is still required before any
  cross-implementation parity claim.
- G-NO-GPU: nothing here was run on a GPU, and emitted skeleton text
  is contract text, not a compilable module. No Doe-over-Dawn, release,
  or browser qualification claim is made or implied.

## What was NOT done, on purpose

- Nothing was pushed to GitHub (task forbids it; the coordinator
  merges and pushes).
- Nothing was written into `~/workspace/fawn-tree` (read-only source).
- No CSL emitter or CSL body was built (quarantined, coordinator
  clarified CSL can be ignored for parity).
- No stub was presented as done: every missing behavior returns
  `TSIR_ERR_UNSUPPORTED` (or `TSIR_ERR_INVALID` for the softmax trap
  case) and is listed above.
- No Mac ASan, Dawn comparison, or qualification results are claimed.
  All runs are Linux GCC + ASan/UBSan on this VM; see test-results.md.
