# CSL quarantine boundary

CSL is preserved but optional. The owner explicitly excluded it from the active
Zig-to-C native parity campaign on 2026-10-25. This scope decision is not evidence
that any remaining native test failure passes, or that physical backend parity
has been qualified.

The canonical [quarantine manifest](../config/csl-quarantine.json) identifies
its compiler/runtime source owners, opt-in targets and pre-quarantine Git blobs.
Source files remain at their original paths so retained source receipts and
historical fixtures stay recoverable. No implementation or sealed evidence is
deleted. The [immutable migration inventory](c-migration/zig-inventory.json)
continues to describe its original baseline, including CSL.

## Default and opt-in paths

- Ordinary Zig install no longer builds or installs the five dedicated CSL tools.
- Native WGSL tests retain native assertions. CSL-only tests have a separate
  generated suite; mixed TSIR tests retain their non-CSL assertions.
- Compiler facades and shared emitter dispatch admit CSL only with explicit
  `-Denable-csl=true`; ordinary Metal, Vulkan, D3D12 and WebGPU paths are unchanged.
- C build/package/Fawn defaults do not depend on CSL or Zig.
- Backend-matrix generation leaves CSL explicitly `not_wired` by default;
  `--include-csl` evaluates the preserved optional receipts without changing native
  readiness requirements. Existing historical matrix reports remain unchanged.
- Default semantic-fixture capture omits CSL. `--include-csl` preserves an
  explicit legacy comparison route; incompatible fixture sets still fail
  comparison rather than silently dropping old artifacts.
- The mixed TSIR bootstrap manifest executable emits native targets by default.
  Its dedicated historical fixture generator opts in to retain the original
  twelve-fixture contract, without rewriting any sealed receipt.

From `runtime/zig`, with the repository-pinned Zig toolchain:

```sh
zig build csl -Denable-csl=true
zig build test-csl -Denable-csl=true
```

Each named CSL tool can still be built separately with that option. Merely
setting the option does not add CSL tools to the default installation. Dedicated
CSL Python runners are explicit optional entry points; their tool builds select
the option themselves. Cerebras SDK execution still requires its own installed
prerequisites and authorization; enabling source compilation grants neither.

## Qualification and re-entry

Native test failures, unsupported hardware and incomplete qualification remain
visible in their existing gates. CSL is not a substitute backend and its optional
status cannot weaken a native oracle. Re-entry requires an explicit scope change,
matching source/fixture provenance, and the dedicated CSL acceptance gates. This
quarantine does not claim a C implementation of CSL.
