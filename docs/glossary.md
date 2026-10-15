# Doe glossary

- `Doe`
  - the repository and program that builds the environment where agents work
- `Fawn`
  - durable browser workspaces: one shared observation, action, and recovery
    service above stock Chromium, defined by the
    [workspace contract](fawn-workspace-contract.md)
  - use the name alone only for this product; the retained uses below are
    qualified
- `DoeRuntime`
  - independently useful GPU execution: the Zig-native WebGPU engine and its
    shared-library artifacts
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
  - the optional forced-Doe Chromium build, smoke, and benchmark surface under
    `browser/chromium/`; also called the browser lane
  - added only when measured benefits justify its compatibility and maintenance
    costs
- `legacy Fawn shell build`
  - the modified Chromium binary named `Fawn` in the retained A/B/C/D lanes,
    release-archive contracts, and `FAWN_*` script variables; not the Fawn
    workspace service
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
