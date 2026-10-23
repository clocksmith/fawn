# Common C style

Prospective floor for new or deliberately changed experimental C code. Follow the
[C charter](CATSCAN.md), [system invariants](../../INTENT.md),
[native adapter rules](../zig/STYLE.md#native-adapter-subset) and child STYLE/contracts.
Existing ABI, numerical, storage and lifetime obligations remain authoritative.
Do not reformat frozen milestones to meet a metric. See
[quality acceptance](../../docs/c-migration/QUALITY_TARGET.md).

## Small typed contracts, one semantic owner
- Use C11, qualified build flags, descriptive domain names and ordinary typed
  records, enums, spans, IDs and functions. Preserve existing public naming/ABI.
  Confine `void *` to declared opaque storage/handles and callback userdata.
- State boundary types, lengths, nullability, alignment, borrowing/retention/release,
  mutation, failure publication and lock/callback effects. Link the owning contract;
  do not create a second policy source, registry, queue or reference count.
- Share a helper only when meaning, ordering, aliasing, lifetime and failure agree.
  Make actual variation typed data/parameters. Compare complete helpers and call
  sites with the direct form; matching spelling alone does not justify extraction.
  No generic graph, macro, allocator, container or callback framework is implied.
- Preserve language-defined evaluation order with named temporaries: C argument
  evaluation order is not a sequencing guarantee. Evaluate effects once, retain
  short circuit and resolved semantic/resource identity, and never replay source.

## Explicit failure, ownership and budgets
- Handle every status; preserve the first error and distinguish rejection, failure,
  uncertainty and success. Unsupported behavior fails visibly, never silently.
  Check arithmetic, alignment, bounds and narrowing before mutation or pointer
  formation. Do not rely on overflow, aliasing casts or invalid intermediate work.
- Prepare privately; publish or mutate failure-state outputs only as the owner
  permits, preserving untouched-output preflight obligations (including wrong ABI/size).
  Keep acquisition/cleanup explicit; never discard an owned obligation. Preserve
  required diagnostics, errno and floating-point state on every exit.
- Keep allocator/storage ownership explicit and honor each owner's no-heap rules;
  do not invent a universal zero-allocation promise. Bound composed traversal,
  recursion/stack, operands, target-format capacity and retained resources, not just
  individual helpers. Spare storage does not raise frozen limits.
- Follow explicit owner lock/callback protocols; default to no foreign callback,
  loader or native procedure under locks. Review any source-bound exception rather
  than silently changing it. Snapshot inputs, keep existing full-call leases,
  and never read potentially freed state afterward. Preserve atomic/publication
  rules; one owner finalizes. Timeout/cancellation is not completion, and uncertain
  completion never authorizes resource reuse or destruction.
- Drop a reference that may be the last one, or join/wait on another owner's
  thread, only with no owner lock held. A finalizer that must release a lease
  while its caller holds a (recursive) owner lock may do so only when that caller
  still holds another lease on the same object, so the drop cannot finalize or join.
  State which lease guarantees this at the call site.

## Readability and decisive checks

Use ordinary four-space indentation, owner brace/naming conventions and one
nontrivial state change per line. Every C/C++/Objective-C source and header has
at most **999 physical lines**. Split cohesive responsibilities,
not arbitrary chunks; keep explanatory comments, diagnostics and provenance. No minification,
line packing, hidden mutable globals, public test-mutation hooks or code-golf macros.
Existing bounded serialization macros do not justify a new framework.

Implement coherent subsystems in parallel; review and run focused checks as code
changes. Exhaustive integrated qualification follows implementation, before any
qualified cutover. Do not create a new planning/report cycle for each small port.
Remove superseded live helpers and guidance recoverably in Git, preserving tests
and historical sealed evidence. Choose the real consumer and smallest decisive
check before editing. Test meaningful
wrong-but-well-typed cases: reversed conditions, wrong resource/field identity,
reordered effects, early publication and premature cleanup. Inject failures at real
boundaries, not a second implementation as oracle. Developer controls remain
separate from frozen independent acceptance. Compilation, sanitizers, target
validation, native consumption and physical execution prove different things.
Bind source/build/test identities, refuse drift and retain first failures.
Every execution claim names where it ran: CPU fallback adapter, or the physical
adapter, driver and host. Print that from the selected adapter at run time rather
than a fixed label, and keep developer runs on one device distinct from
qualification. Later
refactors need separate checks; earlier receipts never qualify changed bytes.

## Mechanically enforced rules

`python3 runtime/c/tools/check_style.py` (also run by `make -C runtime/c check`)
fails on each of these; fix the source rather than adding an exemption:

- **size**: at most 999 physical lines per C/C++/Objective-C source or header.
- **license**: an `SPDX-License-Identifier` within the first three lines. Doe-authored
  code is `MIT`; translated ABI declarations keep their provenance comment too.
- **guard**: every header has an `#ifndef NAME` / `#define NAME` guard or `#pragma once`.
- **pragma**: no `#pragma GCC|clang diagnostic ignored|warning`; fix the warning.
- **flags**: an owner Makefile that defines `CFLAGS` compiles with
  `-std=c11 -Wall -Wextra -Wpedantic -Werror`.

`make -C runtime/c check` also runs `tools/check_globals.py` over every built owner:
each writable process-global object (`.data`/`.bss`, not relocated constants) must be
listed in `tools/mutable_globals.txt` with its owner source and why it is
process-lifetime state, and every listed entry must still exist. Prefer state owned
by an instance, device or caller-supplied storage; a new line there is reviewed like
the state itself.

Frozen evidence is exempt from license and guard only: `independent_tests*` trees and
files whose bytes still match a committed `*.sha256` seal. Editing a sealed file makes
it live source, subject to every rule. `make -C runtime/c check-build` builds every
owner so a changed shared header, enum or status set cannot pass while breaking a
consumer; owner `check` targets remain the behavioral gates.

## Pinned Gamma lessons, not a new engine or a size prediction

The supplied read-only audit supports these mechanisms, without executing Gamma:
[typed effects](https://github.com/clocksmith/gamma/blob/fe381319fdc6917ccb8872d6bbfc174e2806e3a7/projects/algorithm_solver/algsolve/synth/typed_program.py#L84-L102),
[ordered snapshots](https://github.com/clocksmith/gamma/blob/fe381319fdc6917ccb8872d6bbfc174e2806e3a7/projects/algorithm_solver/algsolve/synth/typed_program.py#L373-L428),
[composed budgets](https://github.com/clocksmith/gamma/blob/fe381319fdc6917ccb8872d6bbfc174e2806e3a7/projects/algorithm_solver/algsolve/synth/typed_program.py#L147-L167),
[independent wrong-but-typed controls](https://github.com/clocksmith/gamma/blob/fe381319fdc6917ccb8872d6bbfc174e2806e3a7/projects/algorithm_solver/algsolve/synth/typed_experiment.py#L120-L136),
and [source-drift refusal](https://github.com/clocksmith/gamma/blob/fe381319fdc6917ccb8872d6bbfc174e2806e3a7/projects/algorithm_solver/algsolve/synth/typed_codec.py#L21-L65).
Do not import Gamma's toy-language restrictions into WGSL or its interpreter into Doe.
