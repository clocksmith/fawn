# Independent C compiler milestone 2 acceptance

This source-only, self-contained suite extends the independently frozen milestone
1 oracle without modifying milestone 1's suite or receipts. It tests the explicit
structured neighbor-sum increment, not full WGSL, physical GPU behavior, runtime
migration, or performance. All files inherit the included Doe MIT license.

## Run

Use existing GCC/G++ 14.2 or compatible, Python 3 and an already approved
SPIRV-Tools validator. No install, download, Zig, remote or GPU action is used.
The Linux guard and I/O fault probes require glibc.

```sh
python3 run_acceptance.py \
  --repo-root /path/to/doe \
  --compiler-root /path/to/doe/runtime/c/compiler \
  --library /path/to/libdoe_compiler.so \
  --cli /path/to/doe-compiler-c \
  --validator /path/to/spirv-val \
  --out /path/to/new-evidence-directory
```

The output directory must not exist. Source/build outputs and logs go only there.
The runner verifies both frozen contracts, the unchanged public header and exact
real shader hashes. `commands.json` records commands, flags and exit codes;
`artifact-hashes.json` binds tested inputs. Source-only hashes are in SOURCE.sha256.
Compiler-source snapshots can be supplied as `--compiler-root` when they contain
src/, include/, MILESTONE_1.md and MILESTONE_2.md.

## Frozen black-box coverage

- 117 original admitted M1 fixtures remain unchanged. Five precisely identified
  M1 rejections are intentionally admitted by M2: extra_statement,
  return_statement, constant_used_index, index_constant and index_arithmetic.
  The original no_load_operands case still rejects: u32 literals cannot be
  assigned to its f32 output. All 169 other M1 negative fixtures remain.
- 47 new/lifted M2 admitted fixtures and 71 new negatives. The unchanged real
  neighbor-sum source is SHA-256
  a8a642a54aaca908b707d20666566db8db62dbb5c9bfd518fbe98925da22c416.
  It is an adjacent sum, not a prefix scan.
- Six unsigned comparisons; both outcomes; empty arms; source order; early and
  final returns; both-arms-return; four-level nesting; invalid dead statements;
  single-operand assignments; independent source/destination offsets; constant
  indices; u32 under/overflow; sequential same-binding load/store; unequal and
  empty arrays; read-only/tail preservation; x=0,1,last,first-OOB,UINT32_MAX;
  y/z=0, nonzero and high-bit. Hand-written exact vectors and access traces drive
  expectations, never source templates, reflection, or compiler IR.
- Comparison tokens must be contiguous; split operators are rejected even when
  comments intervene. Sixteen statements/four nesting levels pass; excess
  rejects INPUT_ERROR at the offending original-source token. Dead source is
  still parsed/type/scope/access checked.
- API ABI/version/size, canaries, complete reflection, failure atomicity,
  diagnostic bytes and positions, all insufficient capacities for residual,
  neighbor sum and a large multi-resource fixture, and deterministic parallel
  calls across valid/invalid sources. CLI validates complete artifacts and
  preserves existing files on failure; syscall fault tests include rollback.
- Every admitted API/CLI module is externally validated for Vulkan 1.1. The
  instruction interpreter independently follows branches, returns, unsigned
  arithmetic and Phi predecessors. OOB pointer creation itself is forbidden.
  Unknown instructions are ORACLE_UNSUPPORTED, not candidate-invalid.
- Eleven inherited and twelve new oracle controls include deliberate wrong
  arithmetic, branch selection, return handling, subtraction, wrap-sensitive
  conditions, resource bounds/length, Phi predecessor and untaken-arm effects.
- Ordinary C/C++ source-built consumers; no-Zig shared-library dependency and
  export checks; exact-boundary guard pages; compiler-and-caller ASan/UBSan with
  100,000 deterministic malformed/mutated cases and all 404 frozen fixtures,
  their bounded-length prefixes and embedded-NUL variants. LeakSanitizer is
  explicitly disabled, not passed.

A separately labelled post-black-box architecture-derived probe exercises the
maximum 209 typed instructions with 16 worst-case assignments, eight resources,
63-byte names, exact output and every insufficient output capacity. It is not
counted among the independently frozen black-box fixtures.

## Independence and provenance

M1_IMPORT.sha256 records the original imported suite, copied intact before M2
adaptation. fixtures_m1.py and semantic_checks_m1.py retain the M1 fixture/reference
owners; fixtures.py and semantic_checks.py are explicit M2 overlays. The imported
generic SPIR-V parser remains byte-identical. Its earlier provenance is the Doe
language experiment; original parser SHA-256:
019247bcc7691669d19fa1997456f9bb7963c9b10fdd21cd1d1a8225cc2975c6.
The M2 typed oracle adds OpUnreachable, simultaneous Phi processing and immediate
storage-pointer bounds checking. The compiler source was not read until the
black-box fixtures and exact vectors were frozen and first API/CLI acceptance
had run. Test bodies and expected output lists were authored independently of
compiler implementation. No third-party compiler source or license was imported.

## Boundaries

CPU interpretation and target validation cannot establish physical-driver float
modes, physical robustness, barriers or ordering between invocations/aliased
bindings, device lifetime, backend parity, application adoption or completed Doe
migration. No GPU or LeakSanitizer run or performance/productivity win is claimed.
Full compiler adoption and production owner migration remain separate gates.
