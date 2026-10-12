# AMD projection cost attribution

The [cohort](cohort.json) and [analysis](analysis.json) qualify exact native
execution of retained Tint and Doe programs through the current Doe Vulkan
runtime on the selected AMD adapter. They do not qualify a speedup or explain
the rejected Intel acceleration candidate causally. The historical
[Intel fixture and verdict](../20261011-intel-vulkan-projection/README.md) remain
unchanged. The [manifest](manifest.json) binds source, binaries and raw evidence.

## Declared measurement

The [policy](policy.json) adds a fixed-operation diagnostic for the retained
synthetic projection. Weights, activations, specialization, arithmetic and
independent byte oracles are unchanged. Selected shader bytes are checked
against the retained compilation manifest, validated with SPIR-V Tools and
observed at `vkCreateShaderModule` in separate custody controls. Both compiler
outputs are retained binaries from the earlier experiment, not new compiler
builds. The native runtime is built from the source named in the manifest.

The resident boundary follows the transfer obligations in Doppler's
`w8a8.js`, `command-recorder.js` and `buffer-pool.js`: clear validation status,
record the complete projection, submit and await completion, submit a separate
status copy, map, validate and unmap. The projected tensor stays on the GPU.
Staging is reused as in the warmed buffer-pool case. Native prepared pipelines
and bindings exclude JavaScript allocation, pool lookup and promise scheduling;
this is a transfer-representative native diagnostic, not installed Doppler
inference or an application-replacement comparison.

Separate complete-output checks bracket each process. Verification samples
retain all intermediate copies and CPU comparisons in the timing boundary,
using a single submission. Consequently, the transfer comparison changes both
bytes copied and submission shape; it cannot isolate the cost per byte or
claim that removing debug readbacks accelerates Doppler. It is also not a rerun
of the untouched historical executor: this executor clears status each time
and explicitly settles initial uploads.

Host instrumentation separates recording, submission, completion wait,
readback and CPU comparison. `mapMs` includes readback encoder construction,
submission, mapping and unmapping in resident mode. GPU instrumentation uses
pass-boundary timestamps for quantization, projection and each rescale.
It splits the compute pass and includes query resolution and collection inside
its measured wall boundary. Its perturbation therefore includes pass splitting
and extra GPU work, not just clock reads. GPU durations overlap host waits;
they must not be added to host wall times.

## Observations and limits

The analysis retains every process and paired ratio, including byte-identical
A/A processes and instrumented versus uninstrumented runs. GPU instrumentation
materially perturbs wall time. A/A variation overlaps the apparent compiler and
packed-shader differences, and foreign DRM clients remain present. The
observer records unreadable processes rather than claiming GPU exclusivity.
Mesa disk shader caching is disabled; each process has a fresh XDG cache.
Process-local warmup is symmetric; operating-system file cache is uncontrolled.

The integer projection is the largest measured GPU stage, while complete
resident execution also pays submission, completion and the required status
transfer. Byte comparisons are not the dominant measured host cost. The
observed callback-pump call counts do not support blaming repeated outer
polling alone; those calls may wait inside the runtime or driver. No general
runtime correction is selected from these noisy observations.

The next discriminator is a reserved AMD cohort with the same frozen policy,
followed by one correction in the demonstrated owner. Neither the supplied
W8A8 model's failed quality gate nor prior performance thresholds change.
Transfer to unchanged, independently qualified Node generation and then ONNX
remains open. No Metal, browser or package campaign is introduced.

## Reproduce and validate

Extract the prior `projection.tar.gz` as the fixture and the `matrix-final/doe`
and `matrix-final/tint` directories from its `raw-evidence.tar.gz` as programs.
Build the current native library with `zig build dropin -Doptimize=ReleaseFast`
using the pinned toolchain and an explicit output prefix. Build
`bench/executors/projection-cost-native.c` against
`runtime/zig/vendor/webgpu-headers` with `cc -O2 -Wall -Wextra -Werror -ldl`.
Build the existing Vulkan custody layer with `-shared -fPIC -O2 -lpthread` and
use the retained layer manifest from this report's raw archive.

`python3 -m bench.executors.projection_cost --help` lists the explicit paths.
Use `config/projection-cost.amd.json`, the retained Intel `matrix.json`, and
an explicit Radeon ICD. The runner creates a new output directory and refuses
changed shaders, missing work, bad outputs or unknown completion. No implicit
provider fallback is allowed. Existing GPU clients are observed, not stopped.

Extract [raw evidence](raw-evidence.tar.gz) and run:

```sh
python3 -m bench.executors.projection_cost_analysis \
  <extracted>/cohort-1 <extracted>/policy.json <new-analysis.json>
python3 -m unittest bench.tests.test_projection_cost
```

The corrupted-accumulator physical negative control fails before measured
samples; its complete stdout and failure status are retained. Unit controls
reject missing status, dispatches, verification, settlement, phase observations,
changed timing and promotion. The executed runner snapshot is authenticated
separately: the current runner additionally rejects other fixture shapes and
retains timeout output. Those post-run hardenings do not change the executor or
this cohort's measurements.

Component: benchmark and evidence system. Intent: preserved.
Acceptance evidence: authenticated cohort, raw controls and targeted gates.
Boundary effects: a separate repo-only diagnostic; no compiler, runtime,
Doppler arithmetic, public package or historical verdict changes.
