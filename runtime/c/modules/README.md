# C host module runner

`doe-c-module-runner --module ID --request request.json --policy policy.json`
implements the retained `module-core-runner` contracts without a Zig dependency.
Build with `make -C runtime/c/modules`; `bench-bin` stages the executable under
this owner's ignored `bin/` directory for explicit package selection.

| Module | Preserved execution |
| --- | --- |
| `fawn_compute_services` | Service/kernel admission, dispatch/input budgets and deterministic cost model |
| `fawn_effects_pipeline` | Effect/color-space admission, intermediate budget and deterministic cost model |
| `fawn_path_engine` | Join/dash/geometry budgets and tessellation/raster count model |
| `fawn_resource_scheduler` | Cadence/module admission, ordered grants, pool limits and reuse model |
| `fawn_2d_sdf_renderer` | Blend/clip/sample/atlas/path budgets and render count/cost model |
| `doe_numeric_stability` | Actual serial host f16/f32/f64 matmul, trigger/route evaluation and receipts |

The first five modules are host policy models, as in their retained source. Their
artifact IDs and predicted timing/count fields do not establish GPU execution,
rendered pixels or device performance. The numeric service also runs on the host:
`dispatchCount` is zero, `executionIdentity` is null and `effectApplied` is false.

Inputs retain the source struct field order, integer widths, defaults and typed
numeric coercions for stable hashes, regardless of input object order. Unknown
fields, wrong array sizes and mismatched module/artifact identities fail. Input
files are bounded to 16 MiB each; composed arithmetic is checked. Nonfinite
numerical results fail before publishing results or overwriting receipts: the
retained Zig service can emit invalid bare `inf` JSON for such inputs. Optional
receipt and trace-meta paths retain parent-directory creation and overwrite
semantics. The module trace summary retains the source's snake_case field names.

`make -C runtime/c/modules check` checks 36 source-bound retained results, reversed
field order, missing defaults, exact integers, embedded-NUL string serialization,
policy references, rounding/ties, fallback precedence and failure publication.
`tests/retained.json` records results from a freshly built Zig 0.15.2 ReleaseSafe
runner and hashes every retained contract/implementation source; source drift
fails the check. This is a fixed test oracle, never C build/runtime delegation.
The numeric result hash is verified against its actual serialized bytes; only the
measured duration and its resulting hash are excluded from cross-run comparison.

`make -C runtime/c/modules check-package` also runs all six modules through public
`createDoeRuntime({runtime: 'c'})` APIs, including the numeric convenience method.
It requires `RUNTIME_BINARY` (default: this checkout's scratch CLI build). Explicit
C selection resolves only `doe-c-module-runner`; configured known Zig binaries and
missing configured runners fail without fallback. Production Zig defaults stay
unchanged.

The existing module gate accepts `--runtime c` (builds the C owner) or an explicit
`--runner` with `--runtime c`. Its ten promoted fixture cases pass schema, repeated
execution, expected behavior and hash checks. The gate's omitted root now resolves
the repository root instead of the `bench/` directory.

The [Windows CLI builder](../runtime-cli/README.md) also builds this runner from
the canonical `sources.mk` inventory and runs the same retained host fixtures.
Its shared output owner preserves artifact bytes across memory-backed POSIX
streams and Windows temporary streams; Windows arguments and paths use UTF-8.
