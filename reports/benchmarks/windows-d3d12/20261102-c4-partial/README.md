# Windows completion fix: two partial diagnostic epochs

The measured Doe DLL is `c4fba1230` (SHA-256
`921b6d61505c2d92c0c768ce9250d30ad25756f8b1f6fa758268e82fcbc02555`),
compared with frozen Doe `50cf8feb8` and Dawn `f9997677`. All processes used
32 iterations, including 31 warm iterations, and checked complete GPU output.
These receipts do not measure later source, browser integration or release readiness.

The [formatted-observer epoch](formatted/handoff.json) and
[raw-observer epoch](raw/handoff.json) each executed vector and image cohorts:
192 processes and 6,144 full-output checks per epoch. Each vector window passed;
each image timing window failed. The copy main cohort was unrun in both epochs.
Warmups, rejected preadmissions, accepted executions, raw samples and gate receipts
remain separate. No timed main cohort was retried and no threshold was relaxed.

The first image window lost counter coverage after a formatted-PDH API failure.
The replacement raw observer was checked against SDK layouts, formatted values,
error controls and process churn before its separately frozen campaign.
The [SDK/formatted comparison](observer-validation.tar.gz) retains original
probe samples, layout and execution output. Its image
window retained complete counter coverage but exceeded the frozen foreign-CPU
limit (6.058% versus 5%). Missing own-process counter instances are explicit;
missing foreign values are never substituted with zero. A later CPU probe does
not establish which process caused an earlier rejected interval.

The valid, raw-observer [vector-only result](partial-vector-receipt.json) has
24 interleaved pairs and 3,072 output checks. Warm-operation Doe/previous ratio
is 0.8152, 95% interval [0.0270, 0.9219]; Doe/Dawn is 1.1254
[1.1212, 1.1488]. Thus the fix improves this comparison with the previous Doe,
but Doe still loses to Dawn. Dawn A/A is 1.0027 [0.9801, 1.0177] and passes its
frozen control. These are partial diagnostic results, not a complete campaign.

Both epoch summaries are reanalysis of the preserved process records. Their
report-generation dates are not execution dates. The raw observer's recorded
contract freeze is 2026-11-02T23:10:40.873792Z; the earlier contract has no recorded
freeze timestamp. Driver identity remains unknown as recorded.

[Retention](retention.json) maps original evidence hashes. Each epoch keeps raw
main records as lossless gzip, original contract and build receipt, separate
per-workload windows, summaries versus Dawn and previous Doe, and an admission
archive containing warmups, rejected windows and observer controls. The existing
inventory consumes `window:<workloadId>` roles: failed image timing is incomparable
without changing successful execution or correctness status. No historical row
is superseded or deleted by either partial epoch.
