# CTS provider tooling

`bench/cts/` contains Doe's repo-only WebGPU CTS provider shims.

Current entrypoints:

- [`fawn-node-gpu-provider.js`](fawn-node-gpu-provider.js)
- [`fawn-node-gpu-provider.cjs`](fawn-node-gpu-provider.cjs)
- [`dawn-node-gpu-provider.cjs`](dawn-node-gpu-provider.cjs)
- [`probe-fawn-node-adapter.cjs`](probe-fawn-node-adapter.cjs)

These files are internal operator tooling, not public package contracts. The
[CTS subset runner](../runners/run_cts_subset.py) consumes these providers.
For benchmark entrypoints and evidence ownership, start at
[`bench/README.md`](../README.md).

Ownership and usage:

- treat `bench/cts/` as an internal conformance surface
- keep provider filenames stable when benchmark/config surfaces reference them
- bind retained CTS runs to the provider's materialized adapter identity probe
- set `DOE_CTS_DAWN_PROVIDER_TARGET` to the governed `webgpu` package entry
  when the Dawn shim is used
- add new CTS-facing helpers here only when they are part of repo workflows
