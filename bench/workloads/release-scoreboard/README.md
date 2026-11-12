# Release scoreboard

Five required Doe-versus-Dawn rows. Doe is `doe-gpu` from this tree; Dawn is
the pinned `webgpu` npm package (`bench/package.json`). Both providers run the
same Node executor, scenario, inputs and timing boundary in separate fresh
processes; only the provider module differs. The required list and its
admission rule live in [`config/release-scoreboard.json`](../../../config/release-scoreboard.json):
a backend's release performance claim needs every row measured on that backend
with every workload comparable. A missing, failed, rejected or functional-only
row blocks the claim.

| Row | Workloads | Timing | Output check |
| --- | --- | --- | --- |
| Qwen 3.5 2B decode | `release_scoreboard_qwen35_2b_decode64_warm` | ms per decoded token over 64 greedy tokens after one identical warmup generation (tokens/s beside it) | token ids equal across providers and to the backend's recorded reference |
| Qwen 3.5 2B prefill | `release_scoreboard_qwen35_2b_prefill2k_warm` | generate call to first token, 2,018-token prompt, after one warmup prefill | first token, both ways |
| Cold first token | `release_scoreboard_gemma3_270m_cold_first_token`, `release_scoreboard_qwen35_2b_cold_first_token` | process start to first token; phases below | first token, both ways |
| Draw-call overhead | `release_scoreboard_draw_{1,5,10}k_{static,state_change}` | frame p50 (p95 beside it), encoder creation through queue completion | final frame equals a CPU rasterization and its recorded digest; equal digests |
| three.js scene | `release_scoreboard_three_lit_scene_48_objects` | frame p50 (p95 beside it), `renderer.render()` through queue completion | each provider's frame within the recorded frame's declared tolerance |

Rows 1-3 drive unchanged Doppler (`../doppler`, clean tracked tree) through
`doppler-gpu/compat`. They are not captured plans: Fawn's plan format carries
only synthetic cache-backed buffers, and the record-only capture provider
cannot service Doppler's per-token readbacks, so a captured stream would be a
teacher-forced replay rather than decode. Equal shaders are enforced instead:
each receipt carries the digest of every WGSL module, entry point and override
constant it was given, and the comparator rejects a row whose digests differ.
Timed dispatch, draw and submit counts must also match. The observer wraps the
same WebGPU calls on both providers (pass commands are not wrapped in the
draw-overhead row, whose counts come from the workload), so its small per-call
cost is inside both measurements.

The cold row's phases are sums of WebGPU calls observed inside each harness
window: host loading, upload (`writeBuffer` bytes and time), shader module
creation, pipeline creation and first execution. Translation and native
compilation both happen inside pipeline creation for Dawn and are not
separable at the API boundary. `asyncPipelineCreation.createdBeforeReturn`
records when `createComputePipelineAsync` did its work before returning, as
Doe's Node surface currently does. Driver shader caches and the OS file cache
are uncontrolled; no persistent pipeline cache is enabled on either provider.

The three.js row uses an offscreen `RenderTarget`; the canvas shim only
satisfies `WebGPURenderer`'s context lookup and never presents. Exact frame
digests are recorded, but a lit scene compiled by two WGSL translators can
round a pixel differently, so the oracle is the backend's recorded frame under
the declared tolerance.

References are recorded per backend from executed receipts with
[`record_release_scoreboard_reference.py`](../../tools/record_release_scoreboard_reference.py);
draw rows use the independent CPU rasterization. A backend without a recorded
reference fails its rows until one is recorded.

## Running

Prepare model directories once per host (hashes every shard against
Doppler's tracked manifest; nothing is downloaded):

```bash
python3 bench/tools/prepare_release_scoreboard_models.py \
  --model-id qwen-3-5-2b-q4k-ehaf16 --shard-dir <dir with shard_*.bin>
python3 bench/tools/prepare_release_scoreboard_models.py \
  --model-id gemma-3-270m-it-q4k-ehf16-af32 --shard-dir <dir with shard_*.bin>
npm ci --prefix bench
```

Then, on an idle admitted host, run and gate the full scoreboard:

```bash
python3 bench/runners/run_release_scoreboard.py --backend metal  --out bench/out/release-scoreboard/<run-id>   # macOS
python3 bench/runners/run_release_scoreboard.py --backend d3d12  --out bench/out/release-scoreboard/<run-id>   # Windows
python3 bench/runners/run_release_scoreboard.py --backend vulkan --out bench/out/release-scoreboard/<run-id>   # Linux
python3 bench/gates/release_scoreboard_gate.py --backend <backend> \
  --summary bench/out/release-scoreboard/<run-id>/scoreboard-summary.json
```

`--iterations`, `--warmup` and `--rows` give functional runs that the gate
always rejects. Each row is also a promoted compare profile
(`python3 bench/cli.py compare --list-promoted | grep release-scoreboard`).
