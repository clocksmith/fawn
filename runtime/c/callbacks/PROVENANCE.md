# Callback payload source provenance

SPDX-License-Identifier: MIT. Copyright (c) 2026 Doe contributors.
The repository [MIT license](../../../LICENSE) applies. No external implementation
or new tooling is imported.

Draft baseline is accepted native C futures integration commit
`6b9fea1d581f4d78a7a86a76beddd5a43ff1a965`. The selected future owner and retained
consumer sources are unchanged from the source-verified 25-input trace. All
entries in [BASELINE_SOURCES.sha256](BASELINE_SOURCES.sha256) were rechecked
against this worktree before drafting. Paths in that manifest are repository-root
relative; it binds source bytes, not execution or acceptance of this new owner.
[SEAM_SOURCES.sha256](SEAM_SOURCES.sha256) additionally binds six existing
layout/test-inventory/generator inputs needed by the exact proposed seam.

The actual migrated responsibility is
`runtime/zig/src/native/support/doe_callback_delivery.zig` lines 14–71, SHA-256
`df2a9c3c22885de9f8567dbee0192e293d85d3fe9312920ee9bd94966d3eefe3`.
Its two original inline tests and implementation remain unchanged. The already
accepted future core remains unchanged; its pending list and global identity
source are not copied into this owner.

The [retained integration contract](integration/CONTRACT.md) traces the five
consumer files, seven reply families, one Metal publisher transport and selected
build closure. The existing native Metal-unavailable constructor and Linux stub
are source evidence for a decisive real error-path consumer, not a fabricated
backend or prior execution result.

At drafting time the implementation, independent evaluator inventory, native
consumer execution, sanitizer runs, archive/build costs and acceptance are all
pending. No production/default path has been replaced and no physical backend
qualification has transferred. Original campaign evidence is retained outside
this source tree; draft verification is not a rewritten historical receipt.

## Acceptance

The drafting paragraph above is historical. The independently accepted candidate2
implementation and exact tests are now retained with the [report](independent_tests/REPORT.md).
Candidate1's stateless allocator reconstruction defect and the supplemental
pre-candidate2 fixture remain disclosed there; the C core did not change.

The accepted 1,294-input manifest is
[independent_tests/NATIVE_SOURCE_INPUTS_V2.sha256](independent_tests/NATIVE_SOURCE_INPUTS_V2.sha256).
All entries were rechecked immediately before this acceptance documentation edit.
[SOURCE_INPUTS_CURRENT.sha256](SOURCE_INPUTS_CURRENT.sha256) changes only the
README, CATSCAN and this provenance document to record acceptance. Every compiled
source, build setting, header, layout and test inventory retains its independently
tested bytes. The historical manifest and fixture seals are not rewritten.
The current manifest permits reproduction from the accepted checkout; it is not
a claim that the evaluator executed these later prose edits.

The original callback implementation and inline tests remain byte-identical.
The selected facade and native typed adapter are transitional Zig obligations;
acceptance does not remove them from the full migration goal.
