# Fresh-launch browser comparison

This maintained harness compares selected pages in stock Chromium, Fawn with
Dawn, and forced-Doe Fawn. It preserves fresh processes, counterbalanced arm
order, the host GPU timing lock, provider observations, completion and output
controls, and per-launch receipts. A partial local iteration is diagnostic;
it does not establish release or hardware qualification.

The implementation was promoted from `windows/harness/` in the retained
[Windows archive](../../../reports/benchmarks/fawn-vs-chromium/20261031/raw-evidence-windows.tar.gz).
The [historical comparison](../../../reports/benchmarks/fawn-vs-chromium/20261031/README.md)
and all three raw archives remain unchanged. The Windows version includes the
later Windows provider/memory observations and additional page identities.
Maintained changes add CLI validation, dry-run selection, failed-process
propagation, and argument help; they do not turn historical results into current
results.

Use the canonical front door:

```sh
python3 bench/cli.py browser --help
python3 bench/cli.py browser --config /path/to/browser.json \
  --out bench/out/browser-local --pages compute_atomic_canvas --reps 1 --dry-run
python3 bench/cli.py browser --config /path/to/browser.json \
  --out bench/out/browser-local --pages compute_atomic_canvas --reps 1
python3 bench/cli.py browser-analyze bench/out/browser-local
```

Add `--headless` (or set the boolean `launch.headless` in the configuration) for
background runs without visible windows or focus changes. The mode applies to
every page and arm, is recorded in the schedule and receipts, and cannot change
when resuming a run. The analyzer labels the mode and rejects mixed headed and
headless receipts. Headless diagnostics do not establish visible presentation
performance; retain them separately from headed comparisons.

One page, one repetition, three arms means three fresh launches. One page with
six repetitions covers every three-arm permutation once. The analyzer retains
the historical launch-level bootstrap and practical-effect policy; fewer than
three valid launch observations per arm cannot produce its metric comparisons.
With the standard three-arm configuration, the summary compares Doe and Dawn
with stock, and Doe directly with Dawn in the same Fawn browser.
The additional candidate/control comparison
uses its own recorded seed formula, preserving the original comparisons' random
stream and classification policy. It needs no additional browser launches.
Resume only into an output directory with exactly matching receipt specifications.

The configuration supplies actual browser/library paths and an explicit lock:

```json
{
  "lock": "/tmp/doe-gpu-timing.lock",
  "reps": 12,
  "pages": ["compute_atomic_canvas", "heavy_particles"],
  "launch": {"warmupMs": 2000, "windowMs": 6000, "imageLabOps": 60, "computeOps": 30},
  "arms": [
    {"id": "chrome", "browserPath": "/absolute/stock/chrome", "args": [], "expectDoe": false},
    {"id": "fawn_doe", "browserPath": "/absolute/fawn/chrome", "args": ["--use-webgpu-runtime=doe", "--doe-webgpu-library-path=/absolute/libwebgpu_doe_c.so"], "expectDoe": true},
    {"id": "fawn_dawn", "browserPath": "/absolute/fawn/chrome", "args": ["--use-webgpu-runtime=dawn"], "expectDoe": false}
  ]
}
```

On Windows, `lock` names a lock directory and browser/library paths use Windows
paths. POSIX hosts require `flock`; Windows admission waits for active compiler
and linker processes. Use an idle host with no concurrent GPU work or builds.
The runner does not pause unrelated applications. The page launcher resolves
Playwright through `browser/chromium/package.json` and uses its installed
`playwright-core`; it does not download a browser or install dependencies.

Standalone workload anchors use `bench/browser/fixtures/fawn/`; the unified
cycle uses `browser/chromium/resources/`, alongside the maintained
`compute-atomic-canvas.html`. Served dependency hashes remain in each launch receipt. `launch.repoRoot` can explicitly select
another checkout. Optional upstream pages need their pinned files supplied
through `launch.assetsRoot`; custom workload pages also need their corresponding
`workloads/` HTML. Selecting an unavailable page fails instead of substituting
another workload. Source changes need new receipts.

Native phase attribution is a separate entry point:

```sh
python3 bench/cli.py phase-cohort --help
python3 bench/cli.py phase-cohort run --help
```

It delegates to the existing `bench.tools.dropin_phase_cohort` owner without
changing its preparation, provider build, oracle, sampling or timing boundaries.

New launches record the shared [execution provenance envelope](../../inventory/README.md#new-execution-receipts)
after measured work and browser close. The CLI passes its Python executable;
direct Node launchers require `python3` or `DOE_BENCH_PYTHON`. Host identity and
actual executable/module hashes are observed; missing GPU/driver/source bindings
stay null. This metadata does not upgrade historical receipts or qualification.
