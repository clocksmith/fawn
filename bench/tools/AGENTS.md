# Benchmark command tools

Inherit [benchmark instructions](../AGENTS.md). Own argument conversion, explicit
campaign orchestration and artifact output. Import shared rules from their owners;
do not add another parser, classifier or inventory behind a command.

Keep `bench/cli.py` dispatches stable. Receipt metadata must not extend or alter
timed boundaries. Run affected command tests and generator freshness checks;
schedule inspection does not require running the workload.
