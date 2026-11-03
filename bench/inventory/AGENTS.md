# Registered evidence owner

Inherit [benchmark instructions](../AGENTS.md) and [this charter](CATSCAN.md).

- `builder.py`: orchestration, schema validation, output generation and freshness.
- `sources.py`: verified receipt I/O, shared adapter helpers and dispatch.
- `adapters/`: separate native, phase, browser, workspace and comparison readers;
  no inferred identity or invented samples.
- `model.py`: clocks, logical rows, grouping and scoped current selections.
- `storage.py`: lossless versioned encoding; presentation always uses expanded rows.
- `markdown.py`, `dashboard.py`, `dashboard.js`: projections, filters and navigation;
  never select winners or qualification independently.

The existing builder orchestrates these owners. Keep old import paths as narrow
compatibility facades. Preserve row/series/artifact identities, numeric precision,
null-versus-absent fields and all executions across storage migrations.

Run `python3 -m unittest bench.tests.test_benchmark_inventory
bench.tests.test_benchmark_inventory_views` and
`python3 bench/cli.py inventory --check`. Storage changes also run codec controls.
Inspect dashboard behavior in a browser when changing its interaction.
