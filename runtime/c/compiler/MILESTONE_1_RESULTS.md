# First compiler increment: accepted bounded coverage

Accepted on 2026-10-23 against [the frozen contract](MILESTONE_1.md). The
unchanged residual shader and the retained u32 seed compile through the C
lexer, parser, semantic validation, typed IR and SPIR-V emitter. An independently
source-built library is usable by ordinary GCC C and G++ C++ consumers without
Zig at any build or execution step. This is experimental compiler coverage,
not an accepted replacement of a production Zig owner.

## Independent checks

- 612 API checks and 479 CLI checks pass.
- Each path admits 117 shader variants and passes 117 external SPIRV-Tools
  Vulkan 1.1 validations, for 234 validations in total.
- Each path executes 15,399 instruction-level CPU trials, for 30,798 total.
  The interpreter checks actual emitted instructions and descriptor identity,
  not a reflected operation label or source-template result.
- Empty and unequal buffer lengths, typed-zero OOB reads, discarded OOB stores,
  nonzero output sentinels, untouched inputs/tails, in-place updates, u32 wrap,
  binary32 rounding and special-value classification pass. These tests are not
  hardware floating-point or driver qualification.
- Parallel deterministic calls, explicit ABI/size mismatch, byte extents,
  guard pages, 314 truncated source prefixes, repeated calls, and an allocation
  interposer pass. No allocation/free calls were observed within compilation.
- Independently rebuilt C/C++ consumers, ASan/UBSan with 50,000 malformed
  inputs plus limits/prefixes, and seven injected CLI write/close/link failures
  pass. LeakSanitizer is disabled because it is unavailable in this environment.
- Eleven oracle controls include seven deliberate emitted-module defects and
  explicit unsupported-opcode handling. The source-only portable runner passes
  all seventeen stages.

The independent [suite and reproduction commands](independent_tests/README.md)
are retained beside the [developer tests](tests/). The runner requires a new
evidence output directory and an already available validator; it installs
nothing. Developer tests separately cover 10,244 API checks, 2,048 parallel
compiles, fourteen named-constant cases, ABI layouts, C/C++ inclusion/linkage,
exact ELF exports and hostile host-symbol interposition. Only the declared
public API is exported from the shared library.

## Bound artifacts and provenance

Baseline source: `665475907494d2095a760596218b4c2d363cb436`.
The contract/header were committed before implementation at `043c4cb` and their
[hashes](CONTRACT.sha256) remain unchanged. Both residual WGSL files have SHA-256
`75ba7169ecc1af5f1ee94a94e775c3229fb833e61c0cb16af36a3fc045a31c62`.

Final tested Linux x86-64 artifacts, built with GCC 14.2.0:

- Shared library: `01af920edc146109366d3b92c53e32c2fe3baa1b121bba2acce02e33503524bd`.
- CLI: `94820809cb1b4d6d6ba03c61a04c2b087969ce240335b218689a757f8891cd05`.

Build flags and target commands are in the [Makefile](Makefile). The validator
is Debian SPIRV-Tools 2025.1~rc1-1, invoked with `--target-env vulkan1.1`.
Original imported seed identities and MIT notices are in
[provenance](PROVENANCE.md). Source, binaries and historical receipts from the
prior language experiment remain unchanged.

First failures were retained rather than erased: a developer diagnostic
expectation mismatch was corrected, and ELF review caught six unintended
internal exports before final acceptance. A Linux linker map and a conflicting
host-symbol regression close the latter. An independent evaluator's temporary
ctypes result-lifetime bug was fixed in its harness and the invalid initial
receipt retained; it was not attributed to the compiler.

## Remaining gates

This is Linux x86-64 ABI evidence only. The CLI handles ordinary I/O failures;
its two-file rollback is not a crash-atomic transaction or a hostile-race proof.
The measured GCC static compile frame is 68,736 bytes, excluding callees and
caller buffers. Full-compiler allocation and stack strategy remain explicit
future owners, not a universal no-heap requirement.

The existing residual compilation benchmark consumes MSL, not this SPIR-V
path. No application, source-to-GPU consumer, physical Vulkan/Metal/D3D12,
runtime lifetime, full WGSL, compiler target parity or performance advantage is
qualified here. Production replacement count is still zero. The
[migration gates](../../../docs/c-migration/MIGRATION.md) remain open; the next
increment expands real source coverage without wiring an unqualified runtime.
