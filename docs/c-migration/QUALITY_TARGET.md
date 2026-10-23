# Full-C quality and 50% target

Prospective after M11 `f01effdf070edf2f25cf0f2016c995df1e4d3bab`, not an achieved result.
[MIGRATION.md](MIGRATION.md), owner contracts and frozen receipts remain authoritative
and unchanged. Follow [common C style](../../runtime/c/STYLE.md); later refactors need
new checks. The [counting contract](quality-counting-contract.json) is policy, not a gate implementation.

## Fixed baseline, two mandatory size checks
The immutable [Zig inventory](zig-inventory.json), SHA-256
`9bb2524f32d724a0d74b9a3a76c28c39719aef59bd5cb6b089fa6dc87bbd613d`, binds source
`665475907494d2095a760596218b4c2d363cb436`, tree
`ff1e239161451188a9670e54b0303fb91030d2b4`: 809 files / 222,409 physical lines.
Its 628 handwritten production-role files contain **155,898 physical lines,
including 665 lexical inline-test declarations**. That is not a pure implementation
denominator. Declaration counts are neither AST/executed-test counts nor test-line
spans. Do not subtract 665 lines, infer lengths or regenerate this baseline to win.

An achieved-50% claim requires both:
1. **Headline ceiling: final charged production-delivery source <= 77,949 physical
   lines.** This deliberately stricter complete closure is compared with half the
   frozen handwritten production-file total; it is not itself like-for-like.
2. **Normalized implementation: twice final implementation-only lines <= baseline
   implementation-only lines**, using audited full closures for the same declared
   features/consumers and audited test-only spans on both sides. This supplemental
   denominator never replaces the baseline or raises the headline ceiling.

Unknown spans, missing source/counts or incomplete consumer closure block the claim.

## Count the required delivery, not just `.c` files
Use `len(data.splitlines())` on exact Python `bytes`, without decoding, newline
normalization or stripping. Blank/comment lines and a final unterminated line count;
an empty file is zero and a final newline adds no phantom line. Hash exact bytes.

Charge the unique required source union across declared production builds and
consumers: Doe-owned/maintained implementation, headers, ABI adapters/glue, build
and packaging logic, production-generated source plus its generators/templates/
inputs, and newly introduced or expanded dependency code required by migration,
including required transitive source. Directory, language or shipment status is
not an exemption. Count generated expansion and maintained generator/input code,
with separate subtotals and pinned provenance; a compact recipe cannot hide work.
Shared target paths count once; distinct required duplicate paths count separately.

Pin unchanged platform/toolchain/third-party components in a separate dependency
and installed-footprint ledger, not as an unreported zero or a newly charged entire
libc/compiler. Audit exact required unchanged versus expanded dependency portions.
Report installed/package bytes, including duplicate copies. Non-source assets have
byte totals; encoded behavior needs expanded source accounting, not an asset label.
Do not fetch protected evidence or install dependencies merely for this report.

Reports bind per-path hashes, bytes, lines, roles, production/transitional flags,
owners, consumers and provenance, with distinct views and overlaps disclosed:
- Final charged production delivery, including tests embedded in its source files.
- Baseline/final implementation-only totals after symmetric test-span audit.
- Tests, fixtures/test generators and developer/verification tools separately;
  production-required portions stay charged; do not sum overlapping views.
- Generated inputs/code/expanded outputs and dependency/source/installed subtotals.
- Current transitional active C + required Zig + adapters, with union/subtotals;
  retained obsolete Zig/reference snapshots and historical evidence separately.

Retire adapters from final cost only with consumer/build evidence. Final C-only
permits no Zig implementation/generation/link/runtime fallback. Retained obsolete
reference source is not current production cost. All active owners and consumers
need a source-bound preserved disposition. No externalization into another
language, library, plugin, remote service or generated blob earns a size win.

## Separate test spans fairly
Leave the baseline inventory untouched. A separate artifact binds source hashes to
reviewed half-open byte intervals, test-only purpose, suite and evidence on both
sides, using a pinned structural/parser method plus review. Cover test-only support,
conditional code and generated tests; lexical counts cannot establish spans.
Only lines wholly within audited exclusively test-only spans leave implementation.
Mixed/shared lines remain charged. Assign blank/comment/indentation/delimiter lines
by the reviewed enclosing span, not cosmetic subtraction, and reconcile totals.
Moving inline tests cannot automatically win: exclude old and new test spans
symmetrically and preserve defect detection. Unresolved classifications stay open.

## Quality cannot be exchanged for LOC
Full feature/consumer coverage, independent semantics, ABI/diagnostics, failures,
callback/lifetime and physical-backend gates in MIGRATION remain mandatory.
Explicit rejection is honest partial coverage, not parity. No test deletion,
minification, line packing, weakened limits/oracles or rewritten historical receipts.
CPU fixtures never qualify Vulkan/Metal/D3D12 hardware.

Supplement LOC with bytes/non-whitespace tokens, logical statements and module
dependencies using pinned language-aware methods; report helper call sites,
indirection and semantic ownership. Require concrete readability and failure-path
review, not an aesthetic numeric proof. For matched representative agent-edit
tasks, measure files/guidance inspected, files/owners edited, owner discovery,
decisive-test/passing-patch times, build/check/debugging/blocked time and sample/
accepted-change denominators. Follow [existing evaluation law](../process.md#language-and-cleanup-evaluation),
including regression windows; unmeasured clarity/productivity stays unclaimed.
Use existing records, not a parallel score framework. No coverage,
correctness, readability or qualified-performance regression is excused by size.

Bind candidate commit/tree, dirty-file hashes when relevant, closure, dependencies,
toolchain/flags, this contract, classification artifact and evidence hashes. Refuse
identity drift/missing inputs; distinguish failed, repaired, passed and unrun work.
A working-tree diagnostic is not committed acceptance.
The [pinned Gamma lessons](../../runtime/c/STYLE.md#pinned-gamma-lessons-not-a-new-engine-or-a-size-prediction)
provide a method, not a forecast. The audit's negative
[count-alignment result, lines 40–61](https://github.com/clocksmith/gamma/blob/fe381319fdc6917ccb8872d6bbfc174e2806e3a7/projects/algorithm_solver/literals/count-alignment-v1/README.md#L40-L61)
reduced helper selections but **increased installed storage**; unchanged predecessors
won when eligible, readability was not measured and that report's primitive runtime
suite was unrun. It proves no Doe LOC reduction, quality or parity.
