# Non-CSL TSIR bootstrap tools

These tools implement the remaining facade, manifest-input and oracle
responsibilities from the three named Zig files. The CSL exclusion and production provider selection are unchanged. Retained
Zig correctness fixes are separately identified; source identity receipts always
refer to the actual reference inputs at test time.

From the repository root:

```sh
make -C runtime/c/compiler tsir-bootstrap-tools -j2
runtime/c/compiler/build/doe-tsir-bootstrap-manifest-inputs-c
runtime/c/compiler/build/doe-tsir-bootstrap-oracle-c --kernel gather --inputs INPUT.json
make -C runtime/c/compiler test-tsir
make -C runtime/c/compiler test-tsir-bootstrap-differential -j2
```

The differential target requires the pinned Zig 0.15.2 reference tools, built
with `zig build tsir-bootstrap-oracle tsir-bootstrap-manifest-inputs -j2` from
`runtime/zig`. Normal C tool builds and execution invoke no Zig compiler.
Both tools consume `src/tsir/tsir.h`; behavior remains in the included owners.

The manifest reads the three actual WGSL fixtures, invokes the existing C
parser and semantic validator, adapts typed syntax to the shared TSIR frontend,
plans WebGPU/MSL/SPIR-V, then computes the actual digests. Kernel names select
exactness metadata only. There is no source-pattern shader substitution.
The format's emitter identity hashes actual retained reference emitter sources;
it identifies those reference inputs, rather than claiming identical C source.
The existing Python manifest-lowering schema consumer accepts all nine rows.

Output is staged until success. Array/number preflight, JSON escaping, first
errors, partial acquisition cleanup and caller ownership are exercised. The
oracle preserves CLI options, accepted-and-ignored provenance paths, exact
status JSON, input limits, floating-point conversions and process failures.

Acceptance on Mars (macOS, Apple clang via `gcc`, Zig 0.15.2), based on
`5dec3d50a85585b0822b6dd1d3b48bd7c740f61c`:

| Check | Result |
| --- | --- |
| Six retained TSIR tests | 388 checks passed |
| Source bridge | 76 checks passed, including source release and invalid typed WGSL |
| Manifest API and writer/facade controls | 9 API cases and JSON/error controls passed |
| Manifest differential/negative/schema tests | 6 tests passed; all nine entries byte-identical to Zig; gather-axis mutation also identical |
| Oracle differential and independent hashes | 67 checks passed, including failure boundaries and conversion/overflow cases |
| Target and emitter identities | 43 C checks, 6 actual Zig identities and 6 source/I/O controls passed |
| Allocation acquisition/cleanup | 388 manifest and 211 oracle injected failures passed with zero tracked leaks; UBSan also passed |
| Compiler owner `make all -j2` | Passed, including existing CLI/shared compiler and new tools |
| Cross-owner `make -C runtime/c check -j2` | Attempted; existing label archive guard requires GCC x86_64-linux-gnu, unavailable on this macOS host |

The cross-owner attempt passed style, 35 tool tests and host primitive checks
before the label-owner guard. ASan binaries stalled before main on this host;
the task's attempts were stopped, so ASan verification remains open.

Measured local observations: the integrated build/differential target took
23.22 seconds, the six retained tests 4.23 seconds, and the subsequent compiler
owner build 5.03 seconds (one observation each; mixed existing build state).
No clean-build, runtime-speed, memory-advantage or maintenance improvement is
inferred. This adds a typed adapter and tool orchestration while reusing the
compiler, TSIR, planner, reference, JSON and digest owners; it removes the
previous manifest writer's malformed trailing quote and hardens publication.

The source adapter has a 1 MiB cap, explicit compiler pools and bounded walks.
Constants, aliases, overrides and private/workgroup roots use existing compiler
owners. Switch/continuing, break-if and phony assignments now preserve the
retained TSIR projection. Packed-integer builtin expansion and ldexp argument
materialization now have matching tested semantic projections; unsupported immediate roots
reject explicitly. Broader WGSL equivalence, unrelated interpreter/emitter corners,
GPU execution and browser/release qualification
remain separate gates. This inventory does not establish universal runtime
parity or an advantage over Dawn.

## Dot Linux follow-up

The same base plus the follow-up code passes the retained checks on GCC 14.2
and actual Zig 0.15.2. Five additional mutated sources exercise module constants,
aliases, private roots, workgroup arrays and overrides with byte-identical
manifests. The manifest suite now has seven passing tests. Another 23 control
and 18 expression adapter checks cover continuing/switch, phony assignment,
reference categories, constructors, swizzles and binary64 literal payloads.
The retained 388 checks, 76 source checks, 599 injected allocation failures,
67 oracle comparisons and target identity checks continue to pass.

Linux allocator-test builds select POSIX declarations before the injected
allocator header, and initial source-workspace allocation failure preserves
errno with a regression. Cross-owner style and 35 tool tests pass; the aggregate
build reaches runtime-cli then stops because this host lacks `xcb/xcb.h`.
No full aggregate/global pass is claimed. The earlier dot manifest ASan/UBSan
harness passed with leak detection disabled; LeakSanitizer fails under ptrace.
That earlier sanitizer receipt does not qualify this subsequent source batch.

Production integration is not a source-list-only change: the compiler's public
core ABI forbids allocator dependencies. The current
[host companion](host/README.md) exposes allocating TSIR and explicit-DXC paths
without changing that core contract. External C/C++ consumers, installed-layout
and owned-buffer checks are distinct from physical GPU qualification. The
benchmark generator offers an explicit C-only nine-pair `--native-only` route;
its CSL-inclusive path and independent Zig oracle are not silently replaced.

## Dot v3 bounded parity batch

All standalone TSIR and DXIL sources now compile with the production strict
warning flags, including exhaustive enum diagnostics; no warning suppression.
DXIL diagnostic context now retains its full 96-byte limit without truncation;
979 DXIL controls pass. The six retained TSIR tests now pass 397 checks.
Twenty-one actual Zig/C semantic digests cover controls, packed builtins and
opaque types. At that historical checkpoint two admission differences remained
explicitly reported, not passes; the current correction is described below.
Shared binary64 formatting passes 4,152 exact Zig cases through identity and
four epsilon emitters. Pinned f32 math and NaN min/max fixes pass 3,354 exact
comparisons plus eight qualified numeric-zero ties; see gaps.md. These are
bounded differential receipts, not universal or production-runtime parity.

### Darwin/AArch64 integration

Retained Zig `@exp` uses Darwin `expf`, whose finite rounding and signaling-NaN
handling differ from the portable compiler_rt implementation. The C helper uses
the same native entrypoint on Darwin. AArch64 min/max uses native minimum/maximum-
number semantics, including signaling-NaN quieting and FE_INVALID; the x86-64
path preserves its separately tested reference behavior. The unchanged exact-bit
comparisons, extended with negative signaling NaNs, exercise both value and operand
pair paths. These host-specific corrections do not claim universal GPU semantics.

## Complete descriptor, graph and byte-span contracts

The low-level planner now takes the complete retained target descriptor and
computes correctness identity itself. Old manually created reduced descriptors
must populate the added fields (or copy `tsir_target_descriptor`); source
compatibility with an incomplete descriptor is not claimed. Invalid descriptors
leave empty plans. Identity-only changes alter the stored hash and mismatched
emitters reject. Three descriptors plus all four non-CSL emitter source digests
match seven actual Zig identities, including DXIL outside the bootstrap registry.

The family-hint owner uses an explicit memoized graph work stack rather than
silently stopping after 512 levels. A 4,096-level shared DAG, invalid edges,
cycles and both sides of the retained eight-local alias limit are checked.
The six retained C suites pass 428 checks for this descriptor/depth batch.

Text fields accept optional exact byte lengths: positive lengths preserve
embedded NUL and nonterminated spans; zero keeps legacy C strings, with `""`
representing empty. Callers own input span storage and bounds for the synchronous
call; frontend/planner outputs duplicate and own the full bytes. New span entry
points cover opaque versions and emitter names. Internal bound helpers now
return lengths alongside owned bytes. C/C++ consumers must rebuild for appended
record fields; this is not binary compatibility with the former layout.
`test-tsir-text` passes 33 controls and six actual Zig embedded-NUL semantic,
realization, emitter-version, manifest, target and skeleton identities. WGSL
identifier syntax remains separate from arbitrary opaque TSIR schema text.

These tests do not enumerate every native/browser registry row or qualify any
physical adapter. Historical host receipts above remain historical, not claims
that every current source change ran on those machines.

The retained reference now preserves abstract local-const/inferred-vector types
and lexical constant bindings; the two former source-admission differences pass
the actual canonical source differential. Pre-change rejection receipts
remain separate evidence. The shared invalid scalar-softmax contract is now an
explicit error (Zig `InvalidReductionOp`, C `TSIR_ERR_INVALID`), replacing Zig's
old trap without introducing a fake scalar result. Reference source changes are
identified rather than presented as unchanged-oracle proof.

WGSL-input and source-identity file reading share the private `tsir_file.c`
owner: POSIX and UTF-8-to-UTF-16 Windows handle adapters, regular-file checks,
first-error preservation and explicit stream ownership. The Windows branch
cross-compiles with pinned Zig's `x86_64-windows-gnu` C target and strict
diagnostics; this is not an MSVC, Windows filesystem-runtime, SDK or DXC receipt.
The manifest regression passes a non-ASCII absolute repository path through the
real API, rather than merely changing the working directory.

Typed access projection derives intermediate member/index identities from the
canonical aggregate metadata even when an intermediate AST node has no cached
`resolved_type`. Multi-component swizzles are values with value-loaded bases;
single-component and struct accesses retain reference categories. Direct
struct/vector/matrix/array tests complement actual nested-source differentials;
this is not a general WGSL-IR structural-equivalence certificate.

Focused completion checks are `make -C runtime/c/compiler test-tsir-bootstrap test-tsir-text`, the target/source differential scripts named above, and the
allocation-failure binaries included by the bootstrap target. They exercise
source lowering, control/expression projection, manifest API publication,
oracle behavior, exact byte spans, target/emitter identities, owned-name
cleanup, and rejected-plan cleanup. Byte-span controls also pass UBSan. The
bootstrap Python tests explicitly report unavailable retained-Zig executable
subcases as skipped; they are not a cross-platform qualification. Cross-owner
clean integration and physical-platform receipts remain separate.
