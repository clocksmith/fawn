# Fawn code agent

## Build boldly

Understand → inspect → build → run → diagnose → improve → deliver.

- Start from the requested outcome and the existing implementation. Read the
  relevant code, and compare with working alternatives, before guessing.
- Make the most direct substantive change. Large refactors, replacement
  implementations, aggressive prototypes and speculative ideas are allowed when
  they serve the goal.
- Run the changed system early. Fix the failures you cause; don't stop at a
  plan, scaffold or failing test.
- If the result misses the goal, find the reason in the code and behavior,
  change it and repeat. Sweep parameters only to answer a real question. When a
  strategy is clearly failing, try a materially different one.
- Make routine design and implementation choices yourself. Ask only for a
  consequential product choice, access you lack, or a blocker you can't
  resolve safely.
- Commit coherent, tested progress often, and keep going while the requested
  outcome is unfinished.
- Done means the requested behavior works and was exercised against a
  real-world or competitive baseline. A passing unit test, receipt, report,
  scaffold or milestone is not completion.
- GOALS, INTENT, CATSCAN and charters are references to look up, not reading
  required before editing.
- Report results: what changed, the evidence, what remains, and blockers only
  when they need the owner or unavailable access. No completion percentages.

## Start with the code

This file routes work in the Fawn repository, including its Doe GPU component.
The canonical checkout is `deco/fawn`; do not create a `deco/doe` alias.
Sibling repositories have their own instructions.
Read the code you change and trace its real consumer. [GOALS.md](GOALS.md),
[INTENT.md](INTENT.md), `CATSCAN.md` files, [component charters](docs/component-charters.md)
and the [component index](docs/component-index.md) are references: open them
when a question needs them, not as a checklist before editing. Update a charter
when you deliberately move a boundary.

The table is a lookup for where each kind of change keeps its owner docs, not a
reading list.

| Change | Read at the owner |
| --- | --- |
| Compiler, native objects, backend or ABI | [Default C runtime map](runtime/c/README.md), [C work guide](runtime/c/AGENTS.md), [C style](runtime/c/STYLE.md), relevant [architecture](docs/architecture.md) section |
| Retained Zig reference or explicit legacy route | [Zig runtime map and tests](runtime/zig/README.md), [Zig style](runtime/zig/STYLE.md), owning charter |
| Native C/C++ integration | [Adapter subset](runtime/zig/STYLE.md#native-adapter-subset), adapter's charter and README |
| C runtime (`runtime/c`) | [C work guide](runtime/c/AGENTS.md), [C style](runtime/c/STYLE.md), [migration gates](docs/c-migration/MIGRATION.md); owner-focused checks under the C work guide |
| JavaScript package | [Package README](packages/doe-gpu/README.md), [JavaScript style](packages/doe-gpu/STYLE.md); native addon: [addon guide](runtime/bridge/webgpu-addon/AGENTS.md) |
| Python tools or evidence | [Python style](bench/STYLE.md), owning tool's help/tests; [benchmark front door](bench/README.md) when running workloads |
| Config or contract | [Config style](config/STYLE.md), owning schema and applicable [process law](docs/process.md) section |
| Formal proof or trace | [Lean guide](pipeline/lean/README.md), [Lean style](pipeline/lean/STYLE.md), or [trace guide](pipeline/trace/README.md), according to the changed boundary |
| Improvement agent | [Agent guide](pipeline/agent/README.md) |
| Documentation or retained artifacts | [Documentation owners](docs/INDEX.md), [status policy](docs/status/README.md), [evidence retention](reports/README.md) |
| Performance or claim | [Performance contract](docs/performance-strategy.md), [benchmark front door](bench/README.md), [benchmark skills](SKILLS.md); inspect bound artifacts before making a claim |
| Installation, toolchain or upstream code | [Upgrade policy](docs/upgrade-policy.md), [licensing](docs/licensing.md), integration's existing runbook |

Public/internal/archive classification belongs to
[tool surfaces](config/tool-surfaces.json) and [internal tooling](docs/internal-tooling.md).
The [glossary](docs/glossary.md) owns product names. Use the
[ONNX installation guide](docs/onnx-vulkan-installation.md) for existing ONNX
distribution and [Cerebras front door](docs/cerebras.md) for that lane.

## Work selection

[GOALS.md](GOALS.md) and [the thesis](docs/thesis.md#three-proofs) describe the
current direction; within it, pick the most ambitious useful work and build it.
Commercial strategy and the ordered roadmap remain private.

For systematic Zig review, use the [review ledger](runtime/zig/reviews/README.md)
and `python3 runtime/zig/tools/review_log.py --next`.
[The bounded plan](config/zig-review-plan.json) selects batches; the generated
queue records coverage. Ordinary fixes do not require a full review handoff.

## Development verification

Follow [focused verification](docs/process.md#development-verification) and
[refactor law](docs/process.md#refactor-law). Use the touched behavior's test;
full suites, physical GPU runs and release qualification remain separate.
Once relevant checks pass, continue unless another change, failure or concrete
unresolved question warrants more. A local pass proves only its exercised scope.

## Git workflow

- Commit on this repo's `main` and push to `origin/main`; no branches,
  branch-backed worktrees or PRs unless requested. Never force-push `main`.
- Preserve existing work when changing branches or integrating remote updates.
  Stage only task changes. Report the pushed commit or concrete blocker.
- Keep generated output and local archives out of commits under the
  [retention policy](reports/README.md#retention).

## Handoff

Report what you changed, what you ran and what it showed, with commit hashes.
Fix correctness failures rather than relabeling them or weakening acceptance.
Public claims need current measured evidence, not progress language or a
successful compile. A contract change updates its owner, consumer and checks
together.
