# DXIL C port: gaps, divergences, and unverified items

Honest list of what this directory does NOT deliver. Nothing here is
presented as done elsewhere in this directory's reports.

## Ported in this round (previously sections 1-3)

### 1. The native IR walk (`emit_dxil_native.zig`, 582 lines): PORTED

`src/dxil_ir.h/.c` (IR subset, type interning, std430 layout),
`src/dxil_ir_query.c` (ir_query helpers),
`src/dxil_native.h/.c` plus `src/dxil_native_walk.c` (statement and
expression walks, resource collection, full-module assembly in Zig
order: base types, dx.op declaration, entry function, body, symbol
table, DXIL metadata, serialization, program part, signature parts,
container assembly) port `emit_module` faithfully. The placeholder
behaviors are PRESERVED, not fixed: `param_ref` / `local_ref` /
`global_ref` / `construct` evaluate to constant 0, `construct` and
user calls do not evaluate arguments, `assign` evaluates only the rhs
and performs no store, `if_` emits condition, then-block, then
else-block unconditionally in order, `loop_` emits init, cond, body,
continuing in order, `switch_` evaluates the scrutinee then every case
body in order, `address_of` / `deref` fail `UnsupportedConstruct`,
and collected resources are stored but never read again (only a 65th
bound resource fails). Entry function index out of range returns
`InvalidIr` instead of Zig's panic. Remaining sub-gaps: walk recursion
is capped at depth 64 (fails `InvalidIr`); the native walk does not
use `ir_query.zig` or `layout_utils.zig` (those are ported as helpers
for other consumers, mirroring the Zig file, which does not import
them either).

### 2. The primary emit path (`emit_dxil.zig`, 312 lines): PORTED

`src/dxil_emit.h/.c` ports `dxil_lower`, `dxil_emit_with_diagnostic`
/ `dxil_emit` (native emit plus the validation gate with producer
`native emitter`), and `dxil_emit_toolchain_with_diagnostic` /
`dxil_emit_toolchain` (lower, HLSL emit, temp files, DXC run, read
back capped at 256 KiB, gate producer `DXC`). The HLSL step uses the
bounded emitter in `src/dxil_hlsl*.c` (see section 14 of
`parity-test.md` for its subset limits). Remaining sub-gaps: no
thread-local diagnostic by design (explicit diagnostic output
parameters instead; NULL drops the message); temp-file uniqueness uses
process id plus a counter with `O_EXCL` instead of Zig's
nanosecond-timestamp nonce; DXC read-error messages use `strerror`
text rather than Zig error names; the emit-path diagnostic kind is
always `InvalidIr`, preserved from Zig's `setLastErrorFmt`.

### 3. The translate entry points (`translate_dxil.zig`, 74 lines): PORTED

`src/dxil_translate.h/.c` ports all four variants
(`dxil_translate_with_diagnostic`,
`dxil_translate_toolchain_with_diagnostic`, `dxil_translate`,
`dxil_translate_toolchain`), returning true on success and false on
failure, with the translate status in `*out_error` and the message in
the diagnostic. The analyzer in `src/dxil_analyze*.c` is a bounded
WGSL subset (see section 15 of `parity-test.md`); it is not the full
Zig frontend. The arena-lifetime difference collapses with no
observable difference: nothing is heap allocated, IR storage is
function-local, and output bytes are caller-owned in both variants.
On emit failure the caller diagnostic gets stage `dxil_emit`, the
mapped kind, and the whole internal emission message as detail when
non-empty, preserving Zig's double-prefix shape; on analyze failure
the analyzer diagnostic stands. Remaining sub-gaps: the analyzer
subset limits (struct types must be declared before use; globals may
be forward-referenced via fixup but struct types may not; `for` loops
unsupported; full sema type checking, binding validation beyond
group/binding presence, attribute argument validation beyond the
subset rules, pipeline overrides, `ir_validate`, the robustness
transform, and the opt rewrite are not done). A module the full Zig
pipeline would reject only through one of those passes may be
accepted here.

### 4. Windows DXC runner

`dxil_dxc_run` now has a Windows `CreateProcessW` implementation with
UTF-8 argument conversion, CRT quoting, an explicit inherited-handle list,
concurrent bounded stdout/stderr capture and complete process cleanup.
`tools/test_dxil_windows.py` builds the process boundary and a real child
probe, then requires actual DXC compilation and container validation.
Windows execution remains unverified until that control runs on Windows;
this process implementation alone does not close adapter, D3D12 or release gates.

## Fully ported files

- `dxil_spec.zig` -> `src/dxil_spec.h` (+ tiny `.c` for FourCC bytes).
- `dxil_bitcode.zig` -> `src/dxil_bitcode.h/.c`.
- `dxil_builder.zig` -> `src/dxil_builder.h/.c`.
- `dxil_serialize.zig` -> `src/dxil_serialize.h/.c`.
- `dxil_container.zig` -> `src/dxil_container.h/.c`, plus
  `src/dxil_md5.h/.c` (the MD5 the Zig code gets from the standard
  library).
- `dxil_validate.zig` -> `src/dxil_validate.h/.c`.
- Decision tables of `emit_dxil_native.zig` -> `src/dxil_mapping.h/.c`.
- Portable parts of `emit_dxil.zig` and the error mapping of
  `translate_dxil.zig` -> `src/dxil_toolchain.h/.c`.
- `emit_dxil_native.zig` (full walk and assembly) ->
  `src/dxil_native.h/.c`, `src/dxil_native_walk.c`,
  `src/dxil_ir.h/.c`, `src/dxil_ir_query.c`.
- `emit_dxil.zig` (primary path, HLSL step, DXC fallback) ->
  `src/dxil_emit.h/.c`, `src/dxil_hlsl.h/.c`,
  `src/dxil_hlsl_expr.c`, `src/dxil_hlsl_stmt.c`.
- `translate_dxil.zig` (four entry points) -> `src/dxil_translate.h/.c`.
- WGSL analyzer subset for the translate path ->
  `src/dxil_analyze.h/.c`, `src/dxil_analyze_types.c`,
  `src/dxil_analyze_decl.c`, `src/dxil_analyze_stmt.c`,
  `src/dxil_analyze_expr.c`, plus `src/dxil_diagnostic.h/.c`
  (allocation-free diagnostic, no thread-local instance).

## Intentional divergences from Zig (each fails visibly, never silently)

1. **Zig panics become errors.** Out-of-range function index in
   `create_function_body` returns `DXIL_INVALID_IR` (Zig panics).
   `emit_signed_vbr` / constant encoding for `INT64_MIN` returns the
   defined value `UINT64_MAX` instead of trapping on negating
   `INT64_MIN`.
2. **Integer-cast traps become errors.** Bitcode or container inputs
   longer than `UINT32_MAX` return `DXIL_OUTPUT_TOO_LARGE` (Zig traps
   on the `@intCast`). The container total is accumulated in 64 bits
   and checked, rather than wrapping in 32 bits.
3. **Serialize skips Zig's discarded function lookup.**
   `dxil_serialize.zig` indexes `b.functions[call.callee]` and
   discards the result; the C port does not perform the lookup, so a
   callee value that would index out of range is not an error here.
   The lookup has no observable output effect.
4. **Storage-texture access is a caller input at the mapping layer.**
   Zig reads the access from the storage-texture type itself
   (`st.access`); the C classifier takes one `access` parameter, so
   callers must pass the storage texture's access (not the global's
   access) when the kind is `DXIL_RESTYPE_STORAGE_TEXTURE_2D`. The
   native walk does this correctly: it passes the storage-texture
   type's access, not the global's.
5. **Environment read failure equals unset.** Zig distinguishes an
   allocation failure while reading `DOE_WGSL_DXC` (returning
   `OutOfMemory`); the C port treats a `getenv` failure the same as an
   unset variable. `getenv` failure is not observable on Linux in
   practice.
6. **DXC detail truncation.** Zig includes the full trimmed DXC output
   in its error detail; the C runner includes only the first 511 bytes
   (the message buffer is 2048 bytes). The exit code, profile, entry,
   discovery label, and executable are always complete.
7. **Names are NUL-terminated C strings.** Zig names are byte slices;
   the C builder/serializer use `strlen`. Names containing NUL bytes
   would truncate; shader identifiers never contain NUL bytes.
8. **DXBC hash comment/code disagreement (code wins).** The Zig comment
   claims the first four hash bytes are XORed with the container size;
   the Zig code performs no XOR. The C port implements plain MD5, and
   the test asserts the plain digest against a Python-hashlib oracle.
9. **Fixed IR and analyzer capacities.** Types 128, structs 32 (16
   fields each), globals 128, functions 16, params 32 per function,
   locals 64, expressions 512, expression args 512, statements 256,
   statement children 256, switch selectors 128, switch cases 32,
   entry points 16. Exceeding one fails `OutputTooLarge`. Zig's
   allocator-grown tables have no such caps; a large shader the Zig
   pipeline accepts may fail here.
10. **Recursion caps.** The native walk caps nesting at depth 64
    (`InvalidIr`); the analyzer caps expression, statement-block, and
    unary nesting at depth 256 (`OutputTooLarge`). Zig recurses
    without these caps (and would stack-overflow on adversarial
    input instead of failing visibly).
11. **Query cycle guard.** A cyclic hand-built alias chain stops after
    one pass over the table and returns null/false instead of looping
    (Zig would loop forever). Query recursion depth is capped at 512,
    returning false.
12. **Analyzer subset limits (see also ported item 3).** Struct types
    must be declared before use; `for` loops fail
    `UnsupportedConstruct`; global initializers accept only scalar
    literals (negative literals and composite initializers fail
    `UnsupportedConstruct`); matrix indexing falls back to the base
    type instead of the column vector type; `@id` override values are
    parsed and validated but not stored (the IR global record has no
    override-id field); member fixup failures carry stage and kind
    with no source location. Full sema type checking, binding
    validation beyond group/binding presence, `ir_validate`, the
    robustness transform, and the opt rewrite are not done.
13. **HLSL subset limits.** The header is a single comment line, not
    Zig's `doe_atomic` / `doe_pack` / `doe_unpack` helper prologue, so
    atomic and pack/unpack builtins fail `UnsupportedBuiltin`;
    vertex/fragment stage wrappers fail `UnsupportedConstruct`;
    ref-typed local declarations fail `InvalidIr`; the runtime-array
    write guard is not emitted; the dispatch cbuffer is laid out
    across lines instead of Zig's single-line form (whitespace only,
    no semantic difference); float formatting may differ in the last
    digit for exotic doubles.
14. **No thread-local diagnostic.** Zig's compatibility-diagnostic
    variants read a thread-local last error; the C port takes an
    explicit diagnostic pointer, and NULL drops the message. The
    `dxil_emit` spelling that mirrors Zig's `emit` still takes a
    diagnostic pointer (it is not a separate no-diagnostic overload
    with hidden state).
15. **Arena-versus-allocator lifetime collapse.** Zig's
    `translateToDxilWithDiagnostic` leaks the module into an arena
    and the toolchain variant frees it; the C port allocates nothing,
    keeps IR storage function-local, and returns caller-owned output
    bytes in both variants, so there is no leak in either variant and
    no observable difference.
16. **Temp-file nonce scheme.** Zig names DXC temp files with a
    nanosecond-timestamp nonce; the C port uses process id plus a
    counter with `O_EXCL`. Collision behavior differs only under
    adversarial temp-dir manipulation; both temp files are unlinked
    before returning on every path after creation.

## Unverified items

- **No Zig reference bytes.** No Zig toolchain exists on this VM, so
  byte-level outputs were verified against hand-derived expectations
  from the Zig semantics and a Python-hashlib MD5 oracle, not against
  Zig-generated output. A future run with Zig available should dump
  Zig reference bytes for the serialize/container cases and diff them
  against this port.
- **No real DXC, no GPU, no macOS, no Windows.** No real DXC binary
  was available; the runner and the toolchain translate path were
  tested only against fake shell-script executables that copy a
  pre-made valid container to the `-Fo` path. No GPU execution, no
  Mac ASan run, no release qualification, and no Doe-versus-Dawn
  comparison are claimed.
- **Boundaries above 4 GiB** (container/part sizes near `UINT32_MAX`)
  are guarded by checks but not exercised; allocating such buffers on
  this VM is impractical.
- **Integration into `runtime/c` is not done.** These files live in
  this standalone output directory only. The coordinator owns merging,
  include-path wiring, and any `runtime/c` Makefile/owner changes.
  Nothing was pushed to GitHub and nothing in `~/workspace/fawn-tree`
  was modified.
