# doe-c-runtime: C command-stream runtime CLI

`doe-c-runtime` executes a runtime command stream (`examples/*_commands.json`)
through the public WebGPU C ABI of the C native library (`libwebgpu_doe_c`,
built by [the native owner](../native/README.md)) and writes the same trace
JSONL rows and trace meta that `doe-zig-runtime` writes. It is the first C
replacement for a [stage 6](../../../docs/c-migration/MIGRATION.md) consumer:
the `doe-zig-runtime` CLI that `createDoeRuntime().runBench` in
`packages/doe-gpu` and the bench modules call. It does not replace that CLI by
default; a caller selects it explicitly (the package reads `DOE_BIN`).

```bash
make -C runtime/c/runtime-cli            # builds $(BUILD)/doe-c-runtime and the native library
make -C runtime/c/runtime-cli check-host # host-only tests
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json make -C runtime/c/runtime-cli check
$(BUILD)/doe-c-runtime --commands examples/rmsnorm_tree_commands.json --execute \
  --backend native --kernel-root bench/kernels --trace-jsonl out.jsonl --trace-meta out.meta.json
```

## Admitted input

- Commands: `upload` (`buffer_upload`), `buffer_write` (`write_buffer`,
  `queue_write_buffer`), `barrier`, and `kernel_dispatch` with WGSL kernels,
  buffer bindings (`uniform`, `readonly`/`read_only_storage`, `storage`) and
  `repeatSynchronization` `dependent`. Each kind has a field allowlist.
- Kernels resolve as given, then under `--kernel-root`, then with `.wgsl`.
- `output_oracle` schema 1, `sha256_exact_v1`, `zero_fill_v1` initialization:
  with `--validate-output-oracles` the command runs again on isolated
  zero-filled buffers and the read-back target's SHA-256 must equal
  `expected_sha256`; a mismatch exits 1.
- Arguments as listed by `--help`. `--quirks` needs `--quirk-mode off`;
  `--queue-sync-mode` admits only `per-command`; `--gpu-timestamp-mode` admits
  `off` and `auto` (no timestamps are recorded); `--api` admits only `vulkan`.
  `--adapter auto|physical|software` is this CLI's own: `auto` takes a
  nonfallback adapter, else the fallback (CPU) adapter.

Every other command kind (render, texture, copy, dispatch-indirect and the rest),
field, oracle scope, `repeatSynchronization` value, `semanticOpId` and argument
is rejected with exit 2 before device work.

## Trace artifacts

Rows follow the trace v1 FNV hash chain (seed `0x9e3779b97f4a7c15`); for the
three streams in `make check` the row hashes equal those `doe-zig-runtime`
(db0979828) wrote. The meta carries the Zig meta fields plus an `adapter` object
naming the executing adapter, with `module` `doe-c-runtime`,
`executionBackend`/`backendId` `doe_c_vulkan`, `backendSelectionReason`
`fixed_backend` and no `selectionPolicyHash` (there is no lane policy), plus the
applied `uploadBufferUsage` and `uploadSubmitEvery`.

## Bench comparisons

[`config/doe-runtime-providers.json`](../../../config/doe-runtime-providers.json)
registers this CLI as its own Doe provider (`runtimeImplementation` `c`), so the
compare and claim gates treat its traces as Doe execution while run receipts and
compare reports keep `doe_c_vulkan` and `runtimeImplementation` distinct from
the Zig runtime. `make bench-bin` installs the gitignored
`bin/doe-c-runtime` that the `doe_c_direct_vulkan` executor runs:

```bash
make -C runtime/c/runtime-cli bench-bin
python3 bench/cli.py run --product doe-c --executor-id doe_c_direct_vulkan \
  --workloads bench/workloads/workloads.amd.vulkan.smoke.json --workload-id compute_workgroup_atomic_1024
```

The executor passes `--quirk-mode off`, so workload quirks are not applied.
Workloads outside the admitted input (upload-domain `deferred` sync, render,
textures) fail the run rather than being skipped. The Dawn-versus-Doe strict
Vulkan shader-artifact obligation applies to C traces too; the CLI writes no
shader artifact manifest, so that obligation fails for them.

## Recorded runs

`make check` on lavapipe (llvmpipe, Vulkan CPU adapter), 2026-10-25, source
71c285233 plus this owner: `p0_resource_lifecycle`, `rmsnorm_tree` and
`workgroup_atomic` exit 0, pass `pipeline/trace/replay.py`, match the
doe-zig-runtime hash chains, and the `workgroup_atomic` oracle matches its
expected digest; `--command-repeat 2`, an oracle mismatch and five rejections
behave as specified. A CPU adapter run is not GPU evidence, a performance
result or bench comparability, and no physical adapter has run this CLI.
