# Fawn and Doe glossary

- `Doe`
  - the WebGPU implementation and WGSL compiler that power Fawn, developed within
    the [Fawn repository](https://github.com/clocksmith/fawn); also usable on its
    own from Node, Bun, Deno and native code. `doe-gpu` is its JavaScript package
- `Fawn`
  - the whole project and canonical repository, and the brand of its Chromium
    fork, which Doe powers
  - durable browser workspaces are an optional Fawn feature: one shared
    observation, action, and recovery service, defined by the
    [workspace contract](fawn-workspace-contract.md); call it the Fawn workspace
    service
- `Fawn browser`
  - the Fawn-branded Chromium fork with Doe-owned page WebGPU under
    `browser/chromium/`
- `DoeRuntime`
  - independently useful GPU execution: the native WebGPU engine and its
    shared-library artifacts, with the selected C implementation and retained Zig
    implementation admitted separately
- `DoeLab`
  - the offline loop that tests improvements before promotion
- `DoeProof`
  - the evidence record: provider identity, correctness, fallback, replay, and
    release receipts
- `WGSL compiler`
  - independently usable WGSL analysis and target lowering; `doecompiler` in
    the [strategy contract](../config/doe-product-strategy.json)
- `doe-gpu`
  - the current JavaScript package surface for Doe
- `@simulatte/webgpu`
  - a legacy runtime package family for Node.js and Bun
  - Deprecated — use `doe-gpu` instead
- `@simulatte/webgpu-doe`
  - a legacy transport-free helper package family for compatible WebGPU surfaces
  - Deprecated — use `doe-gpu` instead
- `subpath entrypoint`
  - an alternate export of one package family, not a separate product
- `Chromium runtime integration`
  - the forced-Doe Chromium build, smoke, and benchmark surface under
    `browser/chromium/` that produces the Fawn browser; also called the browser
    lane
  - added only when measured benefits justify its compatibility and maintenance
    costs
- `legacy Fawn shell build`
  - the earlier modified Chromium binary named `Fawn` in the retained A/B/C/D
    lanes, release-archive contracts, and `FAWN_*` script variables; the
    predecessor of the current Fawn browser
- `fawn_*` module ids
  - identifiers of the archived Track B browser-module proposals, retained in
    their schemas, fixtures, and draft contracts
- `Fawn matrix`
  - the retained four-lane benchmark under `bench/fawn_matrix/`
- `hosted demonstrations`
  - the WebGPU artwork pages under `browser/chromium/resources/`; they do not
    implement or qualify the Fawn workspace service
- `benchmark harness`
  - a contract-specific evidence runner under `bench/`
- `pipeline surface`
  - supporting quirk, schema, proof, and trace infrastructure
