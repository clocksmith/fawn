# Doe Lean style guide

This guide is the Lean style contract for `pipeline/lean/`.

## Core principles

- Theorems exist to eliminate runtime branches; every theorem has a
  corresponding runtime callsite or obligation gate.
- Classify every theorem; the classification decides advisory vs blocking.
- Build incrementally: prove base lemmas, compose into stronger theorems.
- Keep proof artifacts reproducible via provenance metadata.

## Module structure

```
Doe/
  Core/         -- foundational types, enums, dispatch, matching
  Full/         -- comparability, workload geometry
  Shader/       -- compute bounds, IR safety
  Generated/    -- auto-generated from config (not hand-edited)
  Extract.lean  -- artifact extraction program
```

- The diagram is representative, not exhaustive.
- `Core/` holds types and theorems mirroring `runtime/zig/src/` runtime logic;
  `Full/` higher-level obligation models and fixture tests; `Shader/` bounds
  proofs for WGSL/IR transforms.
- `Generated/` holds `pipeline/lean/generate_comparability_contract.py`
  output; `pipeline/lean/extract.sh` emits the proof artifact. Never hand-edit
  either.
- Backward-compatibility re-export shims live at `Doe/Model.lean`,
  `Doe/Runtime.lean`, etc. Keep them thin when practical; some legacy shims
  bridge staged migrations.

## File naming

- `PascalCase.lean` for all module files: `Model.lean`, `Runtime.lean`,
  `ComputeBounds.lean`, `IrTypePreserved.lean`.
- Directories mirror the Lean namespace: `Doe/Core/Model.lean` defines
  `Doe.Core.Model`.

## Naming

- **Types and structures**: `PascalCase` (`DeviceProfile`, `Quirk`,
  `WorkloadGeometry`, `DispatchDecision`)
- **Inductive constructors**: follow the file's existing style; single-word
  tags are often lowercase (`.vulkan`, `.metal`), multiword ones camelCase
  (`.copyBufferToTexture`, `.dispatchIndirect`).
- **Theorems**: follow the file's naming family; `camelCase` is common in
  `Core/`, snake_case appears in shader/bounds families
  (`scopeCommandTableComplete`, `gid_component_lt_total`,
  `identityActionPreservesCommand`)
- **Definitions and functions**: `camelCase` (`vendorMatch`,
  `profileMatches`, `scoreQuirk`, `selectForProfile`)
- **Abbreviations**: `camelCase` (`abbrev ExprTypeArray := List Nat`)
- **Hypothesis names**: short `h_` prefix describing the condition
  (`h_wid`, `h_lid`, `h_fit`, `h_fit_comm`)

## Theorem classification

Theorems feeding runtime gates, extraction artifacts, or review fixtures
should carry a classification comment in one of five categories:

| Category | Meaning | Requires Lean |
|----------|---------|---------------|
| `tautological` | Follows from construction (rfl, by-definition) | No |
| `comptime_verified` | Finite enum exhaustion | No |
| `lean_verified` | Quantified over unbounded domains | Yes |
| `lean_required` | Blocking; must pass for runtime gate | Yes |
| `lean_fixture` | Concrete test case with `native_decide` | Yes |

Place it as a `-- Classification: <category> (<reason>).` comment above or
below the theorem, as in the `native_decide` example below.

## Proof style

**Tactic mode** (`by` blocks) is the default:

```lean
theorem toggleAlwaysSupported (q : Quirk) :
    q.scope = .driver_toggle → ∀ cmd : CommandKind, supportsQuirk q cmd := by
  intro h cmd
  simp [supportsQuirk, supportsScope, h]
```

**`rfl`** for trivial reflexivity:

```lean
theorem critical_is_max_rank :
    SafetyClass.critical.rank = 3 := by rfl
```

**`native_decide`** for decidable propositions over finite domains:

```lean
-- Classification: lean_fixture.
theorem strictHappyPathComparable :
    comparableFromFacts strictHappyPathFacts = true := by native_decide
```

**`calc` blocks** for chained inequalities in unbounded proofs:

```lean
calc
  workgroup_id * workgroup_size + local_id
      < workgroup_id * workgroup_size + workgroup_size := h_local
  _ = (workgroup_id + 1) * workgroup_size := h_step
  _ ≤ num_workgroups * workgroup_size := h_wid_mul
  _ ≤ array_length := h_fit_comm
```

**Induction** for unbounded list/structure theorems:

```lean
theorem applyPass_is_prefix (arr : ExprTypeArray) (pass : TransformPass) :
    IsPrefix arr (applyPass arr pass) := by
  induction pass generalizing arr with
  | nil => exact IsPrefix_refl arr
  | cons step rest ih =>
    exact IsPrefix_trans arr _ _ (step_is_prefix arr step) (ih _)
```

## Preconditions

- Name preconditions as explicit hypotheses, not embedded in the goal, and
  document which are GPU hardware guarantees vs host-side checks.

```lean
-- Preconditions:
-- h_wid: workgroup_id < num_workgroups  (GPU hardware guarantee)
-- h_lid: local_id < workgroup_size      (GPU hardware guarantee)
-- h_fit: workgroup_size * num_workgroups ≤ array_length  (host-side)
```

## Lemma composition

- Reuse existing theorems via `have h := theorem_name ...` instead of
  duplicating proof logic.

## Comments

- File header: `--` block naming the module, the runtime code it mirrors, and
  its classification.
- `/-- ... --/` doc comments for definitions and public theorems.
- `-- ...` inline comments for proof strategy notes.
- Section separators: `-- ---` lines for major sections.
- Some legacy files omit headers; add one when editing such a file
  substantively.

```lean
-- Doe/Shader/ComputeBounds.lean
--
-- Bounds safety for global_invocation_id indexing into runtime-sized arrays.
-- Mirrors: runtime/zig/src/compiler/wgsl/ir/ir_transform_robustness.zig
-- Classification: lean_required.
```

## Runtime mirror contract

- Every extracted theorem has one generated `theoremMetadata` row.
- Runtime-elimination metadata binds the Zig source path, mirrored symbol, and
  source SHA-256; extraction fails when a source or symbol is absent.
- Comments may explain the model but are not the runtime mirror authority.
- When a mirrored Zig source changes, update the Lean model in the same change
  or flag it stale in the status log (`docs/status.md`, with dated entries in
  the current `docs/status/*.md` shard).

## Extraction and artifacts

- `Doe/Extract.lean` and `pipeline/lean/extract.sh` extract theorems to
  `pipeline/lean/artifacts/proven-conditions.json`.
- Artifact provenance: toolchain ref, source tree hash, generated contract
  hash, and theorem-metadata generator hash. Schema:
  `config/proof-artifact.schema.json`.
- Zig consumes the artifact at compile time via `-Dlean-verified=true`.

## Toolchain

- The Lean version is pinned in `config/toolchains.json` and the build/check
  scripts.
- Build via `pipeline/lean/check.sh` and `pipeline/lean/extract.sh` with
  explicit `LEAN_PATH`, only when you edit Lean sources or need a new artifact.
- CI (`lean-check.yml`) runs typecheck and extraction; `docs/process.md`
  governs blocking policy.
