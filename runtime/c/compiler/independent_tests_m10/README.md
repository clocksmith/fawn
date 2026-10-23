# Independent M10 evaluator, concrete pre-inspection freeze

This directory prepares acceptance of one isolated compiler increment. It is
not compiler acceptance, a device result, a performance result, runtime adoption,
or completion of the C migration. The original high-level obligations and seal
remain unchanged. REPOSITORY_CONTRACT_BINDING.json binds the final repository
freeze8530b04f6b2171bc24888530dd200e2f4a60983f and records the later narrow parser
counter/stack precision without rewriting the earlier planning provenance.

## Independent source and oracle ownership

- source/model_m10.py derives natural checked layout, reflection graph and raw
  little-endian byte inputs from a small source schema. It reads no target type
  tags, candidate reflection or production packing routine.
- source/fixtures_m10.py and source/extra_m10.py generate the frozen semantic
  fixtures and exact expectations. Cases distinguish type identity, scalar and
  aggregate constant stages, constructors, all vector widths/leaves, operations,
  conversions, scope/dependencies, nested by-value snapshots, access/effect
  ordering, layouts, resources, tails, uniform validity and explicit rejection.
- source/target_m10.py separately interprets emitted SPIR-V1.3 using target
  types/decorations and caller byte spans. It fails on invalid pointer formation,
  unsafe scalar/lane operands before a later select, unsupported instructions,
  or a harness step limit. A limit is unfinished, not successful termination.
  Immutable sparse values handle a billion-element zero/update control without
  field-count flattening. Type traversal and calls use explicit stacks.
- The exact Fraction/IEEE and directed interval helpers are unchanged accepted
  independent evaluators imported at dee5f42. ACCEPTED_IMPORTS.json records them.
  This is intentional independent evaluator reuse, not production code reuse.
- source/controls_m10.py hand-assembles target modules independently of any
  compiler. Five validator-valid mutants change a lane, stride, member offset,
  guard binding and value-TypeId arithmetic family. Each must fail the correct
  byte/safety oracle. Separate controls cover snapshots, sparse zero, malformed
  target bounds, signed zero/NaN comparisons and unsafe exceptional operands.
- source/witnesses contains the exact three frozen fluids sources. Eighteen
  prepared scenarios cover1x1/non-square grids, padded invocation rectangles,
  and independently shortened input/output ranges. Their numerical expectations
  use source-level stencil formulas and exact per-operation binary32 rounding,
  not target op tags. Three witnesses are not coverage of the wider98-source
  corpus, nested-struct family or physical dispatch.
- native/ is a separately sealed public-header-only C11/C++17 evaluator. Its
  README and receipt state both coverage and omissions. The main seal binds that
  native source seal rather than rewriting its independent provenance.
- source/boundaries_m10.py prepares globally active mutual expression/type depth
  checks and optional65533/65534-member target instruction-width boundaries.
  The latter generate source tokens only, not giant expected aggregate values.
  They use separate explicit capacities, not silent growth of ordinary profiles.
- source/cli_m10.py and cli_fault_m10.c prepare binary-only ABI4 invocation, old
  sidecars, no JSON4 option, no-clobber/owned rollback, actual allocator/I/O fault
  ordinals, read/write retry and unowned-file preservation. The instrumentation
  is an attributed copy of the accepted M5 independent wrapper with additive
  ABI4 hooks. It is syntax-checked against frozen public headers only.

No retained WGSL implementation is treated as truth. Exact files/cases/oracles
remain undisclosed to implementation authors until a fixed candidate handoff.
Any evaluator repair after candidate knowledge needs a new version/receipt and
an explicit disclosure classification; never rewrite this seal or old failures.

## Preparation-only reproduction

Use Python3.10+ and the standard library. This does not load a compiler:

    PYTHONDONTWRITEBYTECODE=1 python3 source/controls_m10.py --validator /path/to/spirv-val
    PYTHONDONTWRITEBYTECODE=1 python3 source/run_m10.py --inventory
    PYTHONDONTWRITEBYTECODE=1 python3 native/run_native_m10.py

The existing validator used for self-controls is recorded with its file hash
and Vulkan1.1 options in CONCRETE_FREEZE.json. No download/install is needed.
C/C++ syntax-only commands and the separate native receipt are preserved.

## Candidate execution, only after explicit evaluator handoff

Copy this source directory, use a fixed candidate commit and fresh task-private
/tmp build output. The native runner extracts an immutable source snapshot and
builds both archive and shared consumers without using the moving worktree:

    python3 native/run_native_m10.py --evaluate-candidate --repository REPO --candidate COMMIT --out FRESH_RECEIPT --build-dir /tmp/FRESH_BUILD
    python3 source/run_m10.py --library /tmp/FRESH_BUILD/build/libdoe-native.so --validator VALIDATOR --receipt FRESH_SEMANTIC_RECEIPT
    python3 source/boundaries_m10.py --library /tmp/FRESH_BUILD/build/libdoe-native.so --target-width --receipt FRESH_BOUNDARY_RECEIPT

source/cli_m10.py consumes a fresh CLI and optionally builds the independent
wrapper from the immutable snapshot plus source-built archive; its --help lists
arguments. Keep all sizable binaries in a fresh private /tmp tree and retain
compact receipts/manifests here. Preserve existing sources, artifacts and failed
receipts. An absent validator is recorded unrun, never a validation pass.

## Remaining acceptance gates, not silently credited

1. Re-run the unchanged historical semantic/native/CLI suites from accepted M9
   sources against the fixed M10 build. Preserve original failures and name the
   precise formerly unsupported aggregate cases intentionally lifted. Historical
   frozen ceilings that are explicitly superseded by M10 need a disclosed
   adapter, not rewriting old receipts or claiming the literal old test passed.
2. Produce a full98-source ledger from real sources and unchanged consumer CLI
   routes. Run all three witnesses through source-built library and CLI. No
   cached receipt, absent source or interpreted shader implies runtime adoption.
3. Independently audit actual private records, workspace slicing/lifetimes,
   checked append/target-width/ID arithmetic, no VLA/heap fallback, and measured
   per-function frames/call-graph SCCs against both frozen ledgers. The standalone
   proposed layout probe is not proof of the implementation. Globally active
   parser counters include every mutual expression/type transition.
4. Derived TYPE_SYNTAX cannot be independently configured: TS=floor(T/2).
   The native suite does not claim to reach that exhaustion branch. Audit the
   token/syntax accounting and attempted-append diagnostic. Real2^32 target ID
   exhaustion is impractical; source arithmetic audit plus query-domain and
   malformed-ID controls are distinct evidence, not a fake dynamic boundary.
5. Fault injection, ASan/UBSan, symbols/no allocator imports, source-only native
   integration and deterministic concurrent distinct workspaces remain to run.
   No overlap-rejection test violates the API's disjoint valid-span precondition.
6. Device range/alignment/usage/lifetime, writable overlap, races, GPU validation,
   TranslationInfo/cache/native runtime adoption, physical dispatch and timing
   remain unrun/separate. Short logical bindings are marked safety probes only.

The expected generated inventory is a commitment to future assertions, not a
pass count. Only preparation self-controls and frozen-header syntax checks have
run at this seal.
