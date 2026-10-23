# Independent M6 selected-source acceptance

This is one compact source-only suite. It reuses selected MIT M4/M5 independent
fixture/oracle/native owners once, with ordinary imports. Historical repository
suites are unchanged. `SOURCE_FILES.json`, `SOURCE.sha256` and `IMPORTED_M5.json`
identify the portable source set and retained sources. No baseline binary or
private evidence directory is required. There are no installs or downloads.

## Inputs and execution

Provide a current Doe checkout, optional frozen compiler owner, already-installed
GCC/G++ and the explicitly selected installed spirv-val path. Example:

    python3 run_acceptance_m6.py --repo /path/to/doe --validator /path/to/spirv-val --evidence /fresh/output

Optional flags: `--source-root /path/to/compiler-owner`, `--cc /path/to/gcc`,
`--cxx /path/to/g++`, `--existing-build /path/to/build`, `--baseline-library` and
`--baseline-cli`. Baseline arguments add cross-revision evidence only. Current
ABI/CLI-mode equivalence remains required. All validations use Vulkan 1.1.

`--plan` prints exact commands without executing. `--import-check` checks ordinary
imports without loading a candidate. `--smoke` exercises four decisive M6 cases
only, explicitly not the complete campaign. Use the focused constituent stages
once, then plan/import/smoke for packaging verification rather than repeating the
entire campaign. Native and CLI checks are selectable via native_m6.py --stage.

## Independent boundaries

compute_oracle_m6 executes actual emitted SPIR-V opcodes, resolves inputs only by
BuiltIn decorations/types, resources by descriptor decorations and uniform member
offsets, and Phis by actual predecessor labels. Expected buffers and reflection
are independently authored in fixtures_m6, not derived from compiler reflection.
Nested OOB token loads become zero; subsequent valid loads/stores remain valid.
Index arithmetic wraps as u32 before per-resource bounds guards.

Whole-dispatch tests use coherent builtin tuples and race-free destination
indices. Arbitrary UINT32 input injection is separately labelled synthetic
single-invocation lowering evidence, never a GPU or invocation-order guarantee.
The raw coherent_dispatch_trials count denotes coherent invocation batches:
one full workgroup at a nonzero group coordinate, 48 sampled gather batches
covering valid/guarding lanes, and nine complete copy dispatches. The all-builtin
shader is not qualified across its unexecuted groups, and neither that test nor
gather claims complete-grid execution.
All six unchanged witnesses run through actual unchanged emitter-consumer calls
and output opcode execution. Four witnesses are actual empty compute shaders,
not texture or graphics evidence despite their filenames.

The retained stage keeps 648 historical cases, lifts only 34 exact old exclusions
specified by M6, and preserves malformed/type/access/scope failures. The corpus
stage uses the same 98 paths and records actual first compiler phase, emission,
independent validation and opcode execution separately. It never runs gate main.
The sole dynamic import loads the explicit current-repository consumer requested
by --repo; selected test/oracle modules use ordinary imports.

No physical execution, full compiler migration, production provider switch,
application adoption, TranslationInfo/cache qualification, or combined stack
high-water claim follows from these tests. See frozen M6 contract in the supplied
repository for limits and unresolved gates. Native frame/recursion observations
remain separate from workspace bytes; CLI caller-side frames are report-only.
