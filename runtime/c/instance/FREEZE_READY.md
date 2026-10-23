# Instance 1 installation handoff

This package is ready for final exact-seal review and lead installation under
runtime/c/instance/. It is contract/header/evaluation input only, not candidate
implementation or independent tests. Preserve planning/ and its revision1 seal.

The accepted predecessor is 3582e86472db945cc441eedb3796ec7f1b13d179. Source binding
has been refreshed against accepted Git blobs and matching working-tree bytes;
all reviewed runtime/build/ABI inputs are unchanged. The callback acceptance
prerequisite is closed. Original snapshots remain intact and no deletion is needed.

Install CONTRACT.md, include/doe_instance.h, integration/CONTRACT.md, EVALUATION.md,
PROVENANCE.md, SOURCE_INPUTS.json, SOURCE_INPUTS.sha256 and CONTRACT.sha256 together.
The lead owns corresponding charter/migration/inventory installation when it
admits implementation; do not change source during this handoff. Independent
instance evaluator cases must be authored/sealed separately before candidate code.

Both reviewed precision fixes are carried into the contract and header: recursive
ProcessEvents gets a fresh boundary; allocation and final-cleanup hooks cannot
reenter/resurrect an unpublished/dying instance. No scope expansion or code/test/
build run was performed. Active compiler/M10 files are untouched. Four-provider
rollback, no fake GPU evidence and no production-default promotion remain required.
