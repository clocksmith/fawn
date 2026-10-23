# Native instance: accepted experimental CPU owner

The independent [final report](independent_tests/FINAL_REPORT.md) accepts the
frozen Linux x86-64 ReleaseFast CPU/host boundary. Default production selection
remains Zig. Debug, physical GPU/device/pipeline/imported-texture/presentation,
package/browser adoption and LeakSanitizer are **unqualified**. Native sanitizers
instrument the seven named C owner/bridge units, not retained Zig/stubs/system
libraries. This does not complete the runtime migration.

## Exercised ownership and consumers

One C allocation owns the sole atomic reference count and accepted completion
queue. Copied queue views borrow; full-call leases survive callbacks that free
their payload and release the caller. Cleanup must complete before label removal
and deallocation. Typed payload resources, surface/backend behavior and registry
policy retain their existing separate owners.

Independent GCC C/C++ ownership, allocation/fault/concurrency and boundary tests
pass. Fresh canonical Z/F/FC/FCI native DSOs and separately attributed author
artifacts pass actual C/C++ export/proc consumers and empty instance capabilities.
Retained integration counts are F11, FC16 and FCI23; 24 unchanged default tests,
12 independent native ownership cases and retained ABI crossing checks pass.
The 12 native cases also pass C-only ASan/UBSan and TSan. Thirteen incompatible
build selections reject explicitly; compute/full roots are compile-only.
Metal-unavailable adapter callbacks and real unconfigured Linux surfaces are
error/host-lifetime evidence, not successful device or GPU evidence.

## Bound identity and failures

The original eight-input contract seal remains
`f224ddda8da7c7c0cea8b7dc2bbe4f5ad3771dce01fd6a65130d905037281919`.
The independent portable package seal is
`ab42fe18001adb5b82d268356aed41654ca8818407dcfd0b9ab50979704ce6b1`.
Its [import manifest](independent_tests/IMPORT.sha256) binds fixture source,
original cases, compact execution receipts and failure history. The original
1,693 source inputs plus the separately disclosed 26 tracked Lean provenance
inputs remain immutable; those files are hashed, not executed as Lean.

Preserved limitations include matched contemporaneous default/selected Debug
getProcAddress crashes, unexplained compiler-child termination and a first FCI
900-second timeout with unobserved descendant shutdown. A distinct supervised
canonical FCI retry passed in 417.057 seconds and its process group was empty.
No OOM/root-cause, proven-uncontended, performance or Debug claim follows.
Fixture/closure/sanitizer-startup repairs and the earlier author logical trace-name
fix remain explicitly attributed. No candidate implementation change followed
the final independent handoff. All source snapshots and accepted outputs remain.

## Reproduction and measurements

Use [owner build instructions](README.md) and [independent portable commands](independent_tests/README.md).
The explicit `NATIVE_ACCEPTANCE_INPUTS.sha256` rebinds only README/CATSCAN
acceptance navigation from the immutable original 1,719-input manifest. The
[exact delta](NATIVE_ACCEPTANCE_DELTA.json) identifies both old/new hashes; no
implementation dependency changed. Existing approved toolchains are prerequisites;
no installation is implicit. The lead also replayed all 37 portable normal public
C/C++ commands from the imported driver; its receipt remains separate from the
evaluator package.

Actual instance extents are Z40, F176, FC176 and FCI264 bytes. Observed successful
canonical times were 58.766, 66.311, 66.368 and 417.057 seconds respectively, under
different cache/resource conditions and with failed attempts preserved. These
are task observations, not controlled language/performance comparisons. Exact
artifact hashes, sizes, commands and C/adapter line extents are recorded in
[measurements](independent_tests/MEASUREMENTS.json). No unmeasured edit-cost or
maintainability gain is claimed.
