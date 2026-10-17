# Doe status: TSIR

This is the live status front door for Tiled Spatial IR. Parity receipts under
`reports/parity/` and manifest `integrityExtensions.lowerings[]` entries own
the executable state.

## Current boundary

- TSIR has schema, digest, frontend, planner, reference-interpreter, collective,
  and backend-emitter surfaces.
- Bootstrap-family receipts prove the contract plumbing; they do not prove
  broad real-model backend execution.
- Source-size exceptions have been removed from the TSIR path.

## Active blockers

- Wire WebGPU and CSL backend execution into the parity CLI.
- Produce real kernel-family parity receipts on both declared targets.
- Bind promoted receipts into downstream manifests without parallel identity
  sources.
- Land AOT convert-time lowering only after the execution and parity gates are
  real.

## Ground truth

- Lowering contract: [`../tsir-contract.md`](../tsir-contract.md)
- Receipts: `reports/parity/`
- Historical entries:
  [`archive/2026-04-to-2026-07-tsir.md`](https://github.com/doerun/doe/blob/cf89bce7b9ed096c7d406cffe62dfa9e386e7902/docs/status/archive/2026-04-to-2026-07-tsir.md)
