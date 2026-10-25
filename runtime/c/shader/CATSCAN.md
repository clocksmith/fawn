# CATSCAN: Owned C shader artifacts

Parent: [Experimental C migration](../CATSCAN.md)

## Target
Own immutable compiler-derived module metadata and executable artifacts for
independent consumers and the native frontend.

## Authority
Own allocation, identity, retained views and reference lifetime. The public C
compiler alone owns WGSL validation, specialization, layouts and target meaning.

## Scope
Copied module source/policy, ABI13 validation metadata, selected-entry executable
specializations and retained legacy compilation.

## Contracts
[Public header](include/doe_shader.h) describes caller allocator lifetimes,
explicit capacities and borrowed views. Module-only handles expose symbolic
metadata; executable borrow rejects them. Specialization uses the saved immutable
source/options and additive compiler ABI16, producing one declared entry at index0
and immutable complete sampling-pair metadata from explicit staging capacity.
The additive ABI15 Metal fragment policy is copied before callbacks and owned
with exact sample-mask identity; it never changes the public source interface.

## Invariants
No parser, source substitution, semantic equivalence engine, cache or device owner.
Keys and double representations snapshot before compiler measure/fill; identity
never equates different ordered values. Specialized artifacts retain their module
and own code/value spans. Workspaces and temporary inputs release before success
publication. Failed creation leaves the caller artifact unchanged; failed
replacement preserves existing artifacts. Legacy creation retains its contract.

## Acceptance
[Owner tests](Makefile) cover C/C++ consumption, all targets, source/policy copying,
module/executable separation, selected metadata, exact identity, acquisition faults,
retained lifetimes and sanitizer controls. Compiler, SDK, native execution and
physical qualification remain separate evidence.
- Evidence: [Owner tests](Makefile).

## Non-goals
Pipeline ownership, backend selection or numerical/device qualification.

## Freedom
Internal storage and algorithms may vary within these boundaries.
