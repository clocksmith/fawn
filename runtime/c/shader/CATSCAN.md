# CATSCAN: Owned C shader artifacts

Parent: [Experimental C migration](../CATSCAN.md)

## Target
Own immutable compiler-derived modules and executable artifacts.

## Authority
Own allocation, identity, views and reference lifetime. The compiler alone owns
WGSL validation, specialization, layouts and meaning.

## Scope
Copied module source/policy, symbolic metadata, selected artifacts, legacy compilation
and pointer-free local-build serialization. Filesystem/cache/image policy stays upstream.

## Contracts
[Public API](include/doe_shader.h) owns allocator lifetimes, capacities and borrows.
Executable borrow rejects module-only handles. Specialization uses saved source/policy,
one declared entry at index0, complete staged sampling/translation metadata and exact
override snapshots. Copied ABI15 Metal sample masks preserve source interfaces.
[HLSL profiles](include/doe_shader_hlsl_profile.h) retain explicit ABI19 requirements;
their separate envelope preserves legacy artifact bytes. Legacy imports remain uncertified.

## Invariants
No parser, substitution, equivalence engine, cache or device owner.
Snapshot keys/double representations before measure/fill; never equate different
ordered values. Artifacts retain modules and own code/value spans.
Release workspaces/temporary inputs before publication. Failed creation/replacement
preserves caller outputs and existing artifacts. Legacy creation retains its contract.
Import validates identity, integrity, bounded spans, acyclic types and profile/artifact
consistency. It neither authenticates local files nor validates source through text.

## Acceptance
[Owner tests](Makefile) cover C/C++, targets, copied policy/source, module/executable
separation, selected metadata, profile restoration, identity, acquisition/reentry faults,
retained lifetimes and sanitizers. Compiler, SDK, native execution and physical
qualification remain separate.
- Evidence: [tests](Makefile).

## Non-goals
Pipelines, backend selection or numerical/device qualification.

## Freedom
Storage/algorithms vary within these boundaries.
