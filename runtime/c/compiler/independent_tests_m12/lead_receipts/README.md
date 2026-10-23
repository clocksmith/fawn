# Lead-imported original stack correction receipts

These are byte-identical source/measurement receipts imported after independent
acceptance. They do not alter its original IMPORT.sha256 or cases. The first
candidate exceeds frozen semantic SCC and non-wrapper sum limits. Candidate2
passes fresh complete normal and strict-PIC measurements without changed limits.

The source-only patch changes candidate1 into candidate2. To reconstruct the
rejected candidate in a separate disposable accepted-M12 checkout, run from its
repository root:

```sh
git apply --reverse runtime/c/compiler/independent_tests_m12/lead_receipts/candidate1-to-candidate2.patch
sha256sum -c runtime/c/compiler/independent_tests_m12/lead_receipts/CANDIDATE1_SOURCE.sha256
```

Do not apply it to the working migration branch. It is provenance, not a repair
or accepted candidate. The patch was generated from the exact two immutable
34-file evaluator snapshots and affects only five private builtin/semantic files.
Both source manifests and raw complete stack JSONs are preserved. Local absolute
paths in immutable receipts are provenance, not required replay destinations.
No build/test was repeated for this import.
