# Registered benchmark evidence

This package is the inventory mode of the existing benchmark cube. It reads
registered receipts, preserves observations and generates views. It never runs
workloads or grants public claim authority.

| Job | Owner |
| --- | --- |
| Verify evidence and dispatch formats | `sources.py` |
| Read each retained format | `adapters/native.py`, `phases.py`, `browser.py`, `workspace.py`, `comparisons.py` |
| Separate clocks, identities and current selections | `model.py` |
| Read v2/v3 and write compact v3 without loss | `storage.py` |
| Generate the family overview and complete histories | `markdown.py` |
| Filter and paginate the same observations/selections | `dashboard.py`, `dashboard.js` |
| Dispatch, validate and check all generated outputs | `../tools/build_benchmark_cube.py` |

Run everything through `python3 bench/cli.py inventory`. The maintained
[registration](../../reports/benchmarks/sources.json),
[policy](../../config/benchmark-cube-policy.json) and schemas remain authoritative.

## Readable views

[CURRENT](../../reports/benchmarks/CURRENT.md) is a 16-family overview across five
sections. Each family links to generated `families/<id>.md`, which preserves every
aggregate and unavailable observation. Individual executions and failures stay in
the index. Front-page outcome counts count metrics, not campaign wins; a
loss-only classification never implies that the remaining metrics won. Failed observation counts are separate from metric outcomes;
aggregates and process records are not unique-run counts.

The optional dashboard uses the existing visual theme, filters by OS, actual
machine, section, family, workload, metric and measured build, and paginates.
It initially shows aggregate/unavailable observations. Opt in to per-process
executions; unknown host filters retain each observation's separate identity.

`--check` verifies the index, front page, family histories and any explicitly
requested dashboard without running measurements.

## Storage migration

The cube policy is version 4 (inventory policy version 2), declaring storage
version 3 without changing classification thresholds or grouping. The index now
has `schemaVersion: 3`. Exact shared values are interned in `tables`;
row fields use typed references. A metric stores a template reference plus values
in its template's explicit `valueKeys` order. Null and absent fields, false, zero,
sample counts, units, uncertainty and floating-point values survive expansion.
Interning changes storage, never row, machine, artifact or series identity.

```python
from bench.inventory.storage import load_inventory
inventory = load_inventory('reports/benchmarks/index.json')
# inventory['rows'] has the unchanged logical v2 observation contract.
```

Old v2 indexes remain readable. Existing Python import paths delegate to this
owner. External consumers needing expanded JSON can request it explicitly:

```sh
python3 bench/cli.py inventory --expanded-index-out bench/out/inventory-v2.json
```

Legacy cube v1 generation remains independent and unchanged. The v3 migration
is an intentional serialized-format change; historical measurements and current
selection semantics are preserved exactly.

## New execution receipts

`bench.lib.receipt_provenance.capture_provenance` supplies the shared execution
envelope for native phase processes, browser launches and package workers.
The browser invokes the same Python implementation outside timed work. Through
the CLI it uses the invoking Python executable; direct Node launches require
`python3` or an explicit `DOE_BENCH_PYTHON`.

The helper queries host identity/OS/architecture, accepts actual runtime GPU/driver
observations, hashes named artifact bytes, and accepts source identity only from
a build receipt binding that binary hash. It never substitutes checkout HEAD.
Cold-launch callers compare pre/post file metadata and hash only after execution,
so identity capture does not preload executable pages. Package campaigns retain
their existing input-hash checks. Changed artifacts lose their binding. Unknown GPU, driver, source or comparator fields remain null
with reasons. Provenance grants no correctness, fallback or qualification status.
Historical formats without this discriminator remain untouched.

Focused dashboard UI control (installed Chrome, synthetic data only):

```sh
python3 -m bench.tests.test_benchmark_inventory_views --dashboard-fixture bench/out/inventory-dashboard-fixture.html
node bench/tests/inventory_dashboard_browser.mjs bench/out/inventory-dashboard-fixture.html /absolute/path/to/chrome
```
