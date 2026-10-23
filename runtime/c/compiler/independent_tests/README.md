# Independent C compiler milestone acceptance

This source-only suite independently checks the frozen first C compiler
milestone. It does not use the compiler's tests or infer execution from WGSL
patterns or reflection. Expected results come from fixture metadata and an
exact-integer IEEE binary32 round-to-nearest-even reference. Emitted SPIR-V
instructions drive a bounded CPU interpreter; unsupported opcodes are reported
as oracle limitations, not as invalid compiler output. External SPIRV-Tools
validation remains a separate required gate.

Run with installed GCC/G++ 14 or compatible, Python 3, and an already approved
spirv-val binary. The native guard/allocator/fault-injection checks target Linux
with glibc. The runner never downloads or installs tools, invokes Zig, uses
physical GPUs, or edits compiler source. The evidence output directory must be
new so previous receipts are not overwritten.

```sh
python3 runtime/c/compiler/independent_tests/run_acceptance.py \
  --repo-root "$PWD" \
  --compiler-root "$PWD/runtime/c/compiler" \
  --library "$PWD/runtime/c/compiler/build/libdoe_compiler.so" \
  --cli "$PWD/runtime/c/compiler/build/doe-compiler-c" \
  --validator /absolute/path/to/spirv-val \
  --out /absolute/path/to/new-evidence-directory
```

## Coverage and boundaries

- Both byte-identical real residual shaders are hash-checked before compilation.
- Retained u32 increment and named-constant positives and still-applicable
  rejections, f32 and u32 load+load, all resource declaration permutations,
  mixed direct/aliased indices, scope/type/binding failures, and resource limits.
- Descriptor identity/type/access are independently recovered from SPIR-V.
  Each actual load/store checks its own resource's bounds in the interpreter.
  Empty and unequal lengths, nonzero output sentinels, untouched input/tail
  bytes, UINT32_MAX invocation, normal/subnormal, signed zero, infinities/NaNs,
  half-way rounding, and u32 wrap are exercised. NaNs compare classification.
- API byte extents/canaries, ABI version/result size mismatch no-writes,
  atomic rejection, original diagnostic positions, repeated/parallel calls.
- Complete CLI reflection, little-endian modules, preservation/no publication
  on invalid source or I/O/argument failures, and injected write/close/link
  failures including rollback after the first artifact was published.
- Ordinary independent C and C++ consumers compile against a source-built C
  library. Guard pages exercise exact input/output and truncated sources;
  allocator interposition observes allocation/free calls inside compile.
- ASan and UBSan instrument both the compiler and 50,000 deterministic malformed
  inputs plus boundary/prefix cases. LeakSanitizer is explicitly disabled.
- Deliberately broken emitted modules demonstrate sensitivity to wrong typed
  arithmetic, descriptor aliasing, wrong resource lengths, missing guards/stores
  and output incorrectly gated by an input. Unknown opcodes remain unsupported.

CPU results do not establish driver float controls, physical GPU robustness,
full WGSL coverage, backend/lifetime correctness, production migration or any
performance/productivity advantage. The CLI transaction checks are ordinary
failure-path tests, not cross-filesystem crash-atomicity or hostile-race proofs.

## Provenance

The generic SPIR-V parser in spirv_oracle_base.py and retained fixture definitions
were copied from Doe's independently evaluated prior language experiment. The
original files remain unchanged. Prior parser SHA-256:
019247bcc7691669d19fa1997456f9bb7963c9b10fdd21cd1d1a8225cc2975c6.
The typed multi-resource interpreter, exact-integer float reference, expansion
fixtures and acceptance orchestration were authored independently before source
review. Native guard/fuzz probes extend the previous independent harnesses.
All use the Doe repository's MIT license.
