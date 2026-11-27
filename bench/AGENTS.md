# Benchmark work

[The charter](CATSCAN.md) and [Python style](STYLE.md) are references. Use
[the existing CLI](cli.py); preserve command names and frozen measurement rules.
The sections below collect owner guidance; they are a lookup, not a reading list.
The [inventory guide](inventory/AGENTS.md) retains its distinct storage/view map.

## Shared measurement constraints

Record actual loaded artifacts, provider, completed output, process status and
machine evidence, rather than requested settings. Keep metadata collection outside
timed work; distinguish fresh-process, startup, warm, shutdown and memory scopes.
Missing inputs/output, hidden fallback and failed processes must stay explicit.
A receipt does not qualify another machine, provider or execution surface.

Preserve failures, exact identities, unknown dates and historical interpretation.
Diagnostic, comparable, qualified and publication-authorized statuses are distinct;
inventory discovery grants none. Thresholds belong to versioned policies. Never
run a substitute workload to make missing evidence pass, or upgrade old receipts
with newly inferred provenance.

Run affected module tests and a touched generator's `--check`. Use focused positive
and adversarial controls for changed admission rules. Global gates run when their
governed configuration changes. Documentation, navigation, storage and ownership
changes do not require GPU or broad qualification campaigns. New measurements
need a named measurement or correctness question and the relevant execution lane.

## Browser harnesses and executors

[browser/](browser/) owns browser process/page measurements in the existing
harnesses; [executors/](executors/) owns one product's execution, timed intervals,
output/trace capture and process status. Inventory adapters only read receipts;
comparisons and qualification belong to their respective owners. Use shared
provenance when its receipt contract applies. Missing browser inputs fail
explicitly. Harness changes use affected harness tests and schedule dry runs;
receipt-shape edits alone do not launch a browser campaign.

## Application and native comparisons

[external-projects/](external-projects/) owns pinned application preparation and
unchanged-application comparisons; runtime implementation stays at its component
owner. Retain source, model/input, provider, completion and independent-output
evidence. Exercise affected adapters' focused controls.

[native-compare/](native-compare/) owns workload/config recipes and CLI contracts;
reusable implementation lives in [native_compare_modules/](native_compare_modules/).
GPU execution and resource lifetime stay in the runtime. Preserve provider, cache,
timing, output and cold/warm boundaries. Single-provider telemetry and loss-only
classification cannot establish a comparative win. Exercise affected native-compare
controls; registered normalization belongs to inventory, not another parser.

## Campaigns and command tools

[runners/](runners/) sequences existing executors, receipt writers, oracles and
gates. Preserve each phase's input, completion and failure boundary, cancellation,
unknown completion, worker ownership and retained failures. Resume only when
contracts and artifact identities match. Preflight and report-generation clocks
cannot manufacture measurement facts. Test affected orchestration separately from
running a measurement campaign. `run_blocking_gates.py --scope report --report <path>`
runs the report checks. Add `--with-backend-selection-gate --backend-selection-lane <lane>`
to check the selected backend. Release qualification uses the default full sequence.

[tools/](tools/) owns argument conversion, explicit campaign orchestration and
artifact output. Import parsers, classifiers and inventory rules from their owners;
keep CLI dispatch stable. Schedule inspection does not require running a workload.

## Admission, expected outputs and regression controls

[gates/](gates/) checks versioned contracts and reports actionable failures;
[oracles/](oracles/) owns CPU references, numerical acceptance and expected-output
artifacts independently of timed implementations. Bind input bytes, shape, dtype
and declared exactness/tolerance. Do not obtain expected outputs through the GPU
path under test or weaken a frozen oracle after a loss. Unsupported references
stay explicit; exercise incorrect-output rejection as well as positive cases.

Use `python3 -m pytest bench/tests/test_<module>.py` for affected modules.
[tests/](tests/) uses synthetic receipts and independent expected outcomes.
Preserve negative controls for wrong units, missing evidence, stale artifacts,
unknown machines, callback failures and accidental qualification. Test public
consumer behavior, not source spelling or copied policy literals; see
[test-value guidance](STYLE.md#test-value). Hardware acceptance remains separate.

## Shared helpers and inventory

[lib/](lib/) helpers stay focused and importable without running commands.
Native/package legacy cube consumers still use them; registered inventory rules
belong to [inventory](inventory/AGENTS.md). `lib/receipt_provenance.py` records
observed host/artifact identities for new receipts; checkout HEAD does not prove a
binary's source or qualification. Compatibility imports delegate to the canonical
owner. Test changed helpers and their consumers.
