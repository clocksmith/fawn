# Compiler source provenance

The current [accepted M10 owner](MILESTONE_10_RESULTS.md) extends the same
Doe-owned C implementation with typed layout, compact aggregate constants,
semantic access/call owners and target/reflection support. It does not translate
or embed a retained vendor/Zig compiler. New source files preserve MIT notices.
The independently authored source-only tests and exact disclosed adapters are
retained under `independent_tests_m10`; original prior tests and contracts are
unchanged. The following seed/increment records remain historical provenance.

This increment starts from copies of the independently developed C compiler seed in `doe-language-experiment-20261023/phase2/maintenance/c`. The original seed files, binaries and evidence are immutable. They are not compiled from their old location by this build. No Dawn, Tint, wgpu, or production Zig compiler implementation was copied or translated.

The seed was Doe-owned experimental work under the repository's MIT license. This copy preserves that license through the repository [LICENSE](../../../LICENSE), with SPDX and Doe contributor notices on C source/header files. The SPIR-V enum names/numeric values identify standardized instructions; no vendor compiler is embedded.

## Imported seed identity

SHA-256 at copy time, before migration edits:

| Seed file | SHA-256 |
| --- | --- |
| `src/cli.c` | `1ceb2f6d30d8122b2b745e867e7530ed6f1ad168e97bdecc0db145ec3226e2ae` |
| `src/compile.c` | `301e4aadb45ec5ca28ee1ce82a11898aa84a67f243165ebe0ac5b411481f0b7c` |
| `src/internal.h` | `4c621452704897e7a63edb54b90b67c59c35a7653e1b3aadb9892a68422990fc` |
| `src/lex.c` | `8fd9b9c7cfa0f612877e0991f261684c8f940b98c20714ac9a984f1b60825a61` |
| `src/parse.c` | `782ba24c8b5ec36304766c2d1c236edd8dc040a6fcb6db58e85ed362ad03f8de` |
| `src/spirv.c` | `240c289cb6b1c49676bd96c2334e13a28cdfe37f1b7c48f58e240e97c60b134f` |
| `src/validate.c` | `09e20fb9316d74e60e951d1ffa2aad859efbf04f1124cd3f56798f9f9a976fe0` |

Copied developer-test inputs from `doe-language-experiment-20261023/phase2/c/tests` (byte-identical to `phase2/maintenance/c/tests`), plus the maintenance constant regression:

| Seed file | SHA-256 |
| --- | --- |
| `tests/test_api.c` | `c7927cfee770a4ce4fa6f95b38b8ba0fff2bc0efe1b4f2cf1bcf85d6a3df4f41` |
| `tests/test_cli.py` | `ce639e45c83d33fd4936200c05edce0e20841273374d7d9d686acae7100bfee6` |
| `phase2/maintenance/tests/named_constant.c` | `f24a1dd98e8cbf485cb6526d4bd28ca094a79690339ea029f479d08d7e209fe8` |

## Changes in this increment

- Lexer retains the seed's whitespace/comment/ASCII/position behavior; the explicit token capability grows from 512 to 2048.
- Parser and validation expand from one u32 buffer to typed u32/f32 resources, two typed operands and an optional invocation-x alias; unchanged residual WGSL is real source coverage, not a template or existing application-adoption proof.
- The old shared guard is replaced by independently guarded robust resource operations. The SPIR-V owner lowers typed `OpIAdd`/`OpFAdd` and per-load zero-valued `OpPhi` merges.
- The former experimental slice API is replaced, not silently ABI-aliased, by the frozen version-1 compiler header, eight-binding reflection and explicit ABI/size admission.
- CLI and developer regression tests are adapted from seed copies. Newly added tests remain developer tests; independent acceptance evidence belongs to a separate evaluator.

The original source baseline and contract hashes are recorded in [MILESTONE_1.md](MILESTONE_1.md) and [CONTRACT.sha256](CONTRACT.sha256). No original source, historical evidence or production provider selection is changed by this increment.

## Structured neighbor-sum extension

Milestone 2 extends the accepted native C owners rather than translating retained
Zig implementation. The original seed and accepted first-milestone source remain
available in history. The unchanged real neighbor-sum shader is input, not an
implementation template. The parser adds bounded statement ranges and index
atoms; validation adds typed values and control markers; SPIR-V emission adds
unsigned comparisons/subtraction and termination-aware structured selections.

The version-1 public header and both frozen contracts are unchanged. Developer
instruction tests grow separately from the independent evaluator. No production
Zig owner, source fixture or prior experimental source is modified. This extension
adds source admission only: no physical GPU, runtime adopter, performance or
completed migration claim follows from it.

## Existing emitter-consumer integration

Milestone 3 changes only the CLI's optional-reflection admission and conditional
reflection-file ownership. It preserves the accepted M2 library sources, header,
SPIR-V bytes and reflection schema. The unchanged real benchmark gate is a
consumer, not imported compiler implementation. No seed, historical independent
test, frozen contract, existing consumer or production source is modified.

The pre-change executable and library, source identities and actual
required-reflection rejection are retained with the isolated milestone evidence
before rebuilding. New developer tests exercise both output modes; linker
wrappers inject local file failures only into dedicated test executables, never
the shipped CLI or shared library. The source build uses the already installed
GCC toolchain and validator, with no Zig or new dependency.

## Typed scalar expressions and immutable locals

Milestone 4 adds the separately owned `numeric.c` checked integer-rational
converter and general parsed expression/local records, rather than recognizing
scale/ReLU source text or translating a Zig implementation. Original source
fixtures, public header, CLI and compile/publication owner are unchanged.
Historical independent suites and their receipts stay immutable; the new M4 suite
records imported test-source identities and exact intentional grammar lifts.
See [accepted scope and provenance](MILESTONE_4_RESULTS.md).

## Milestone 5

Flat uniform resources, typed layout/member loads, versioned reflection and caller-workspace ownership are independent C work under the frozen M5 contract. No production Zig source was translated or removed. The selected independent evaluator sources retain their MIT notices and imported-source manifest; results and exact artifact identities are in MILESTONE_5_RESULTS.md. Primary WGSL/WebGPU layout sources are linked in the frozen contract.

## Scalar function and math owner

Milestone 9 extends the independently written C pipeline with typed module
functions/calls and selected scalar builtins. Retained Zig sources and unchanged
WGSL witnesses identify semantics and consumers; they are not mechanically
translated implementation. The MIT source notices remain. Independent evaluator
sources, numerical oracles, original seals and precisely named historical adapters
are preserved under independent_tests_m9; no third-party implementation is copied.
See MILESTONE_9_RESULTS.md for acceptance and explicit limits.
