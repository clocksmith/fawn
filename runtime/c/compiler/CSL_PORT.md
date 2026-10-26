# CSL C port, one-page summary

Label: CSL C port, untested / not run, parity not verified. Nothing pushed.

## What was done

Ported all 52 CSL Zig files (17,425 lines) from clocksmith/fawn main d25e075 to C,
in ~/workspace/doe-c-work/csl-port/src/, as a single local patch set ready for one
future snapshot-push. This reverses the earlier NO CSL exclusion for this patch only;
it does not claim full Zig/C parity.

- emit/csl: 37 Zig files, 13,508 lines -> 49 C files, 2,830 lines. Done 7, partial 30, missing 0.
- spatial/csl: 8 Zig files, 2,525 lines -> 17 C files, 3,863 lines. Done 3, partial 5, missing 0.
- TSIR CSL-only: 7 Zig files, 1,392 lines -> 12 C files, 3,134 lines. Done 6, partial 1, missing 0.
- Total: 78 C files, 9,827 lines, all under 999 lines per file, SPDX MIT, header guards.
  Plus 3 UNRUN test drivers (597 lines) and 6 worker mapping/test files (1,324 lines).
  Coordinator files: parity-mapping.md, parity-test.md, gaps.md, test-results.md, this summary.

## How it was simplified

Shared helpers, not mechanical transliteration: one emit-helper header, one JSON-string
writer, one PE preamble, one layout skeleton for 13 patterns, one attention core,
one contract/validate/exec table set, table-driven spatial bindings, merged identical
tool parameter branches, shared TSIR compute tail/imports/headers, gated silu/sigmoid
clamp-block collapse. Dead paths removed only where provably dead in the port
(csl_desc_str, csl_classify_from_ir, unused typedefs/macros). Every simplification is
listed in parity-mapping.md, with simplifications deliberately not taken also listed.
No simplification was allowed to silently change behavior or turn a missing case into
a silent stub; missing cases return CSL_ERR_UNSUPPORTED visibly.

## What is NOT parity-proven

File-porting is now complete for CSL (and, with the earlier DXIL/TSIR non-CSL pushes,
for all non-CSL files), but behavior parity is not proven for any of the 52 files.
Behavioral tests were not run, per user instruction. Only worker-reported gcc
syntax/compile checks ran (parsing/compiling only, see test-results.md).
Partial emitters are pattern templates or descriptor forms, not full Zig template text.
Key unported areas: full IR walk and IR classification, embedded WGSL fixtures,
JSON manifest/execution parsing, Python runner full per-launch body, TSIR digest,
KV full_per_pe, driver/cslc execution, GPU/Cerebras execution, and all cross-module
integration (emit semantic ops with TSIR bodies, spatial tool/materialize with emit host-plan).

## What still remains, beyond CSL

DXIL bounded frontend/HLSL, Windows DXC runner unimplemented, production wiring
(sources.txt, libdoe_compiler.so, strict-flag wiring), documented semantic differences
(frontend depth limits, softmax failure, embedded-NUL names, shader admission),
full cross-platform runtime parity not established, native API stubs and registry gaps
not re-verified at d25e075, Fawn launcher gap if still present.

## Confirmation

Nothing was pushed to clocksmith/fawn, nothing was committed, nothing was written to
~/workspace/fawn-tree (stale at 2fa21299) or to ~/workspace/fawn-current (read-only
source at d25e075). Remote head read-back after this work still shows
d25e075341219fc398dedb9286c2669a1b966221. The old ~/workspace/doe-c-work/csl/
directory was not overwritten; this csl-port/ directory is the new deliverable.
