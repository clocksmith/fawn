# Preserved fixture changes

## Surface/readable-storage v2, before candidate source handoff

Original complete executable fixture package and original seal remain under
pre-surface-repair-v1/. No original assertion result or candidate failure was
removed. Candidate code remains unread/unexecuted as of this amendment.

Author disclosed its own too-small invalid-device fixture failure on 2026-10-23.
Evaluator independently checked accepted predecessor source and emitted typed
layout diagnostics from a fresh Git archive of that predecessor. FC layout is:
DoeDevice size42736/alignment8/magic42720; DoeInstance size176/alignment8/magic168;
DoeSurface size96/alignment8/magic80. These are fixture precondition facts, not a
C-instance layout contract. Two failed attempts (missing full link dependencies,
then missing C-import includes) and the successful compile-log layout diagnostic
are retained in baseline-receipts. No hardware call occurred.

- Native invalid-device fixture now supplies initialized zeroed bytes with exact
  @sizeOf(DoeDevice) and @alignOf(DoeDevice). No valid device is fabricated.
- Public legacy invalid-handle storage is65536 readable bytes/alignment8 instead
  of64, sufficient for these pinned recognizers. All bytes at actual markers are
  nonmatching. Device rejection still exercises actual post-retain cleanup.
- Actual live surface-as-wrong-kind-instance is tested only in FCI, whose C-prefix
  recognizer can legally inspect its live first bytes. It is not called on F/FC
  because their retained typed recognizer would read beyond the96-byte surface.
  This restriction respects the original readable-storage precondition; it does
  not fix or hide a newly introduced candidate behavior.

The separately authorized INSTRUMENTATION.md clarification at commit
1aa0114489c0962108b71cbbde422b45c64053d3 supplies test_surface_allocator, only under
builtin.is_test and stable through all live surfaces. Two additional native tests
realize the already sealed pre-retain OOM and matched surface destruction cases.
This hook-name supplement is disclosed after author input, not blind API discovery.

## Pre-candidate self-review strengthening v3

Before any candidate source handoff, evaluator self-review noticed C02 test code
used one state address with distinct vtables despite the inventory promising
distinct object/wait state identities. Wait allocation now uses its own typed
WaitState address, checks its binding, and preserves the same observation object.
The wrapper can now explicitly inject alignment0, already required by C17, rather
than treating zero only as disabled injection. Prior test source and v2 seal are
retained under pre-allocator-strengthening-v2/. No candidate result motivated
these corrections and no expected observation was weakened.

## Accepted-baseline proc target correction v4

Before candidate handoff, the new consumer was compiled and run against the exact
accepted historical F DSO3277a65c... (separately labelled, not current source-build
evidence). It caught an incorrect test assumption: the native proc manifest is
not the first lookup decision. The actual retained resolveLocalProc maps every
instance API exercised here to its public wrapper before manifest/native lookup.
The first assertion and a diagnostic-only rerun are preserved in baseline-receipts.
Immutable predecessor source confirms the exact public targets. The corrected
fixture now requires stronger exact public function identity for all those entries,
including surface creation/release, and adds a separate explicit native-export
pass instead of mistaking proc lookup for native pointer equality. Earlier test
source and seal remain in pre-proc-resolution-v3/. This is a baseline-oracle test
repair, not candidate acceptance relaxation; the frozen inventory required the
actual local resolver target all along.

## Pre-candidate exact wait pairing strengthening v5

Self-review while candidate handoff was pending added an external, mutex-protected
allocation observation list for wait records. Every wait free now checks the exact
original pointer, state, vtable, requested extent and alignment even during nested
and other-thread waits. This realizes the already sealed matching-allocator
expectation without inspecting object internals or owning an instance reference.
The previous fixture and V4 seal remain in pre-wait-pairing-v4/.

## First native compilation, after exact handoff

The sealed fixture failed to compile before executing any test: four vtable-token
comparisons required explicit optional-anyopaque pointer casts; callback publisher
requires a comptime choice, so the fixed two-mode loop is now inline; generated
WGPUStringView data is a C pointer, not an optional pointer. These are six
mechanical fixture type fixes only. Assertions, modes, data and expected outcomes
are unchanged. All original V5 files and seal are in pre-native-compile-v5/, and
the original failed native-focused-fixtures receipt remains. No candidate source
was changed.
