# Native catalog classification and methodology reconciliation

Scope: full 127-workload native catalog (`bench/workloads/metadata/backend-workload-catalog.json`)
evaluated across all four available hardware backends (`apple-metal`, `intel-vulkan`,
`windows-d3d12`, `windows-amd-vulkan`; `excluded-host` omitted per operator instruction).

Measured Doe source is **`6258de498566bbe3e0cf59ba3e672d177574d7a6`**, not current
HEAD. These aggregate diagnostic results do not qualify later runtime source.
Current diagnoses must be checked against current implementations: D3D12 already
batches writes and pools one idle upload allocation, and Metal texture writes
already submit without an internal synchronous wait. The retained diagnosis
sections below describe their measured/reviewed checkpoint.

### Reproducible classification and sampling limitations

The [versioned classification policy](classification-policy.json) owns one rule:
Doe/Dawn median ratio **>1.05**, bootstrap 95% CI lower bound **>1.00**, and
**Doe's absolute median >=0.05 ms**, or **>=0.5 MiB** for `max_rss_mib`.
The absolute floor is not the Doe-minus-Dawn median gap. Applying this rule to
the retained, unrounded aggregate statistics reproduces every existing ranked
row: 1,409 phase/CPU/memory losses, including 237 cold-plus-warm operation rows
and 129 warm-operation rows. Generate counts, rankings and report text with the same classifier using the
command below; outputs stay in ignored `bench/out/` rather than duplicating
retained evidence in the source tree.
The alternative `median gap >0.01 ms` rule happens to reproduce warm counts,
but does not reproduce all phase rows or totals.

```sh
python3 bench/cli.py catalog-report \
  --catalog reports/benchmarks/catalog-20261101 \
  --policy reports/benchmarks/catalog-20261101/classification-policy.json \
  --out bench/out/catalog-classification \
  --check-retained reports/benchmarks/catalog-20261101/ranked_losses.json
```

This reproduces classification from retained summaries, not the bootstrap
statistics themselves. Per-pair arrays and process traces were temporary and are
not retained in this catalog. Do not infer independent output correctness or
recompute missing samples from medians and confidence intervals.

The [recovered runner](recovered-runner.py.gz), retained byte-for-byte for
inspection rather than as maintained tooling, explains the 3/5-pair exceptions.
[Lineage](sampling-lineage.json) records its hash and matching JSON data between all
four local summary copies and the tracked summaries (Windows formatting differs). The runner was not bound
into the prospective contract, so this is corroborated retrospective lineage.
After pair zero it selects 3 pairs when the slower warm operation is >=1,200 ms,
5 when >=250 ms, otherwise 7. Only rows below 50 ms with CI relative half-width
above 5% top up to 11 pairs. Thus the frozen `7..11` description omitted exceptions.

There is a further timing limitation: command workloads start with two passes,
but the runner switches later samples to one command repetition when pair zero's
slower warm operation is >=150 ms. In that branch it aliases cold rows as warm
rows. Those aggregated warm results cannot establish independently measured
steady-state performance. Raw samples are unavailable to identify and repair
each affected aggregate retrospectively; a new governed measurement must retain
them and measure distinct cold/warm work. The original result JSONs, rankings,
contracts and seals remain unchanged.


The [original sealed report](README.md), including its original methodology and diagnosis wording, remains unchanged as historical evidence. This reconciliation is the current interpretation of those retained aggregates.
