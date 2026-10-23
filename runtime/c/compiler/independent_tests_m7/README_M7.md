# Independent M7 acceptance

Source-only suite for the isolated C compiler milestone. It needs Python3,
installed GCC/G++, installed Khronos spirv-val, candidate compiler source, and
unchanged repository witnesses/consumer. It does not need historical binaries,
archived receipts, network, Zig, a GPU, or sys.path manipulation.

Run from any directory:

    python3 /path/to/suite/run_acceptance_m7.py \
      --repo /path/to/doe --source-root /path/to/frozen/compiler \
      --validator /path/to/spirv-val --evidence /fresh/evidence/directory

Use --plan to inspect exact commands; --import-check imports source modules
without reading/executing the candidate. --existing-build accepts an explicitly
identified library and CLI; --stages selects focused stages and does not claim
full acceptance. Output directories must be fresh to preserve first failures.
The public native job consumes capacities/workspace owned by the caller; it does
not invoke or link Zig. The compiler source snapshot must remain unchanged.

M7 additions are fixtures_m7/semantic_m7/compute_oracle_m7, api_job_m7/native_job_m7/
cli_job_m7 and witnesses_m7/corpus_m7. Earlier selected M1-M6 modules are retained
verbatim from the previous independent source bundle, except their own imported
paths already recorded in the M6 provenance. The old README is preserved verbatim as README_M6.md; old SOURCE manifests are
historical M6 identities, not this bundle's M7 manifest. Ordinary Python imports
resolve the flat source bundle; no archived directory is consulted.

The interpreter reads actual emitted SPIR-V IDs, instructions, binding/member
and builtin decorations. Expected outcomes derive from frozen source contracts
and raw separately packed resource data. It has no WGSL source recognizer.
Function memory, branches, loops, robust load/store guards and Phi predecessors
execute dynamically. A step bound reports unfinished and is not shader
termination or a correctness pass. Unknown instructions are oracle limitations,
not defect detection. Mutation controls distinguish validator and instruction
execution detection. CPU interpretation is not physical GPU execution, runtime
adoption, performance qualification or completion of the full migration.

The same98-source consumer ledger separates skipped, newly emitted and prebuilt
observations. The five mandatory unchanged witnesses and one additional frozen
source-admissible else-if/index lift are explicit. All target validation requests
use --target-env vulkan1.1. Legacy API capacities and rejection categories remain
separate from intentional job/binary/reflection-v2 CLI lifts.

Amendment provenance: M7 amendment1 initially scoped empty module declarations
to job calls; amendment2 superseded that restriction with shared all-mode
admission where the base source's capabilities are supported. Superseded tests
and hashes remain as provenance, not active stages. The active front door uses
job_empty_module_all_modes_m7.py and retained_m7.py. The latter imports unchanged
historical fixture bytes and adds exactly struct_semicolon, trailing_statement
and excess_tokens to named admission lifts. The misleading historical
excess_tokens source has only576 tokens; separate2048/2049 fixed-API cases retain
the actual token boundary. No interior malformed/type-invalid cases are relaxed.

First candidate receipts are preserved even when an evaluator expectation was
narrowly corrected. The M7 final category changes concern only unrecognized
switch/local-const/unary-minus syntax (PARSE_ERROR or UNSUPPORTED) and a checked
out-of-range integer magnitude (SEMANTIC_ERROR). The public API correction stops
requiring untouched valid workspace on an invalid source/output span, while
retaining that requirement for ABI/size/invalid-capacities/workspace preflight.
These are evaluator corrections, not implementation fixes. Use the acceptance
summary to reconcile original receipts and focused rechecks; do not treat every
historical receipt as a standalone current verdict.
