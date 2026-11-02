# Benchmark work

Read [the charter](CATSCAN.md) and [Python style](STYLE.md). Use
[the existing CLI](cli.py); preserve command names and frozen measurement rules.

| Job | Owner |
| --- | --- |
| Register, normalize, select, store or display evidence | [inventory](inventory/AGENTS.md) |
| Browser process/page measurements | [browser](browser/AGENTS.md) |
| External application preparation and comparisons | [external-projects](external-projects/AGENTS.md) |
| Native compare recipes and reusable native compare helpers | [native-compare](native-compare/AGENTS.md) |
| One-product execution and trace capture | [executors](executors/AGENTS.md) |
| Multi-stage campaigns and qualification sequencing | [runners](runners/AGENTS.md) |
| Evidence admission and frozen acceptance | [gates](gates/AGENTS.md) |
| Independent expected outputs | [oracles](oracles/AGENTS.md) |
| Command entry points and campaign orchestration | [tools](tools/AGENTS.md) |
| Shared helpers and retained cube compatibility | [lib](lib/AGENTS.md) |
| Independent regression controls | [tests](tests/AGENTS.md) |

Run affected module tests and the touched generator's `--check`. Do not run GPUs
or broad qualification campaigns for navigation, storage or ownership changes.
Preserve failures, exact identities, unknown dates and historical interpretation.
Leaf instructions describe local ownership; do not copy this contract into them.
