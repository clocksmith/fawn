# Campaign sequencing

Inherit [benchmark instructions](../AGENTS.md). Own multi-stage orchestration over
existing executors, receipt writers, oracles and gates. Each phase keeps its
original input, completion and failure boundary.

Preserve cancellation, unknown completion, worker ownership and retained failures.
Resume only with matching contracts and artifact identities. Do not manufacture
measurement facts from preflight or report-generation clocks.
Run affected orchestration controls; a campaign is separate measurement work.
