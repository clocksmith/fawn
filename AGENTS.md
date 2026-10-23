# Doe code agent

## Start with the owner

This file routes Doe work; sibling repositories have their own instructions.
Read [GOALS.md](GOALS.md), [INTENT.md](INTENT.md), and every `CATSCAN.md` from
root to the files being changed. Find them with `rg --files -g CATSCAN.md` or
[the component index](docs/component-index.md). Follow the
[charter protocol](docs/component-charters.md); update affected charters with
an explicitly authorized boundary change, never to excuse a failure.

Locate the real consumer and trace the complete affected path before editing.
Choose its smallest decisive check before implementation. Read only the
applicable owner documents below; there is no repository-wide reading checklist.

| Change | Read at the owner |
| --- | --- |
| Compiler, native objects, backend or ABI | [Runtime map and tests](runtime/zig/README.md), [Zig style](runtime/zig/STYLE.md), relevant [architecture](docs/architecture.md) section |
| Native C/C++ integration | [Adapter subset](runtime/zig/STYLE.md#native-adapter-subset), adapter's charter and README |
| C migration (`runtime/c`) | [C work guide](runtime/c/AGENTS.md), [C style](runtime/c/STYLE.md), [migration gates](docs/c-migration/MIGRATION.md); `make -C runtime/c check` before committing |
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

Follow [GOALS.md's constraints](GOALS.md#strategic-constraints) and
[the selected proofs](docs/thesis.md#three-proofs). Commercial strategy and the
ordered roadmap remain private. Historical reports and status notes cannot
start competing work or reopen rejected campaigns.

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

State component, intent preserved or changed, acceptance evidence, and boundary
effects using the [charter handoff](docs/component-charters.md#mechanical-validation).
Report [cleanup and iteration measurements](docs/process.md#language-and-cleanup-evaluation)
without claiming unmeasured improvements. A contract change updates its owner,
consumer, schema/migration and applicable checks together.

Fix correctness failures rather than relabeling them or weakening acceptance.
Describe concrete deltas, dependencies and measured process state; never invent
engineering timelines. Public claims need current source-bound evidence, not
progress language or a successful compile.
