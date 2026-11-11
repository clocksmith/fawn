# CATSCAN: Owned C shader artifacts

Parent: [C runtime](../CATSCAN.md)

## Target
Own immutable compiler-derived modules and executable artifacts.

## Authority
Own allocation, identity, views and reference lifetime. The compiler alone owns
WGSL validation, specialization, layouts and meaning.

## Scope
Copied source/policy, symbolic metadata, selected/legacy artifacts and pointer-free
serialization. Filesystem/cache/image policy stays upstream.

## Contracts
[API](include/doe_shader.h) owns allocators, capacities and borrows.
Executable borrow rejects module-only handles. Specialization uses saved source/policy,
one entry at index0, complete staged metadata and exact override snapshots.
Copied ABI15 Metal sample masks preserve source interfaces.
Explicit ABI20 workspace retains one compiler analysis through publication;
no hidden compiler handles or output-size guesses.
[HLSL profiles](include/doe_shader_hlsl_profile.h) retain explicit ABI19 requirements;
their envelope preserves legacy bytes. Legacy imports remain uncertified.

## Invariants
No parser, substitution, equivalence engine, cache or device owner.
Snapshot keys/double representations before measure/fill; never equate different
ordered values. Artifacts retain modules and own code/value spans.
Release workspaces/temporaries before publication. Failures preserve outputs; `doe_shader_create_module_diagnostics` retains allocator-owned pre-fatal notices without publishing a module; legacy creation stays unchanged.
Import validates identity, integrity, bounded spans, acyclic types and profile/artifact
consistency without authenticating files or re-validating source text.

## Acceptance
[Owner tests](Makefile) cover C/C++, targets, copied policy/source, module/executable
separation, selected metadata, profile restoration, identity, acquisition/reentry faults,
retained lifetimes and sanitizers. Compiler, SDK, native and physical qualification
remain separate.
- Evidence: [tests](Makefile).

## Non-goals
Pipelines, backend selection or numerical/device qualification.

## Freedom
Storage/algorithms vary within these boundaries.
