# DXIL C port: gaps, divergences, and unverified items

Honest list of what this directory does NOT deliver. Nothing here is
presented as done elsewhere in this directory's reports.

## Unimplemented (no code, no stub)

### 1. The native IR walk (`emit_dxil_native.zig`, 582 lines)

The statement walker, expression walker, resource collection over a Doe
IR module, and full-module assembly (`emit_module`: base types, dx.op
declaration, entry function, body, symbol table, DXIL metadata,
serialization, program part, signature parts, container assembly) are
NOT ported. They operate on Doe IR types (`ir.Module`, `ir.Function`,
`ir.StmtId`, `ir.ExprId`, globals, bindings) for which no C equivalent
exists in this directory. Porting the walk without the IR would be a
stub, so it is listed here instead.

What IS ported from this file, as pure functions in `dxil_mapping.*`:
stage mapping, compute/vertex/fragment parameter-builtin rules, the
builtin-name to dx.op table, unary/binary opcode tables, resource
classification, and the DXIL metadata values. The container assembly
order (SFI0, ISGN, OSGN, DXIL) is documented in `parity-test.md`
section 7 and is trivially composable from `dxil_container.*` once a
caller supplies the bitcode.

Consequences: the placeholder behaviors of the Zig walk are also not
ported, including `param_ref` / `local_ref` / `global_ref` /
`construct` / `member` / `index` evaluating to constant 0, the
`assign` statement performing no store, and `address_of` / `deref`
failing with `UnsupportedConstruct`. A future IR port must decide
whether to preserve those placeholders or implement the missing
semantics; they are recorded here so they are not silently inherited.

### 2. The primary emit path (`emit_dxil.zig`, 312 lines)

`emit`, `emitWithDiagnostic`, and the diagnostic plumbing (including
the thread-local compatibility diagnostic and `lastErrorMessage`) are
NOT ported: they need the Doe IR module and the native emitter from
item 1. What IS ported from this file: the constants
(`MAX_OUTPUT`, `DOE_WGSL_DXC`, `PATH`), the toolchain environment
parsing, the DXC profile/argv/run logic, and the output-validation
gate (`dxil_validate_output`, producers `native emitter` and `DXC`).

Also not ported from this file: HLSL generation for the DXC fallback
(needs the HLSL emitter, a different directory's concern), temporary
file management under `.zig-cache/doe_wgsl_dxil`, and the 64 KiB DXC
output cap as anything other than the runner's capture budget.

### 3. The translate entry points (`translate_dxil.zig`, 74 lines)

`translateToDxil*` (four variants) are NOT ported: they need the WGSL
analyzer and the Doe IR. What IS ported: the error mapping (identity
over the shared status set, exposed as
`dxil_emit_error_to_translate_error`) and the re-exported constants.
The arena-lifetime difference between the two `WithDiagnostic`
variants (one leaks the module into an arena, one frees it) has no C
equivalent yet and is not modeled.

### 4. Windows DXC runner

`dxil_dxc_run` on Windows returns
`DXIL_SHADER_TOOLCHAIN_UNAVAILABLE` with a message saying the Windows
runner is not implemented. The POSIX runner is fully implemented and
tested on this Linux VM. No Windows result is claimed.

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
4. **Storage-texture access is a caller input.** Zig reads the access
   from the storage-texture type itself (`st.access`); the C
   classifier takes one `access` parameter, so callers must pass the
   storage texture's access (not the global's access) when the kind is
   `DXIL_RESTYPE_STORAGE_TEXTURE_2D`.
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

## Unverified items

- **No Zig reference bytes.** No Zig toolchain exists on this VM, so
  byte-level outputs were verified against hand-derived expectations
  from the Zig semantics and a Python-hashlib MD5 oracle, not against
  Zig-generated output. A future run with Zig available should dump
  Zig reference bytes for the serialize/container cases and diff them
  against this port.
- **No DXC, no GPU, no macOS, no Windows.** No real DXC binary was
  available; the runner was tested only against fake shell-script
  executables. No GPU execution, no Mac ASan run, no release
  qualification, and no Doe-versus-Dawn comparison are claimed.
- **Boundaries above 4 GiB** (container/part sizes near `UINT32_MAX`)
  are guarded by checks but not exercised; allocating such buffers on
  this VM is impractical.
- **Integration into `runtime/c` is not done.** These files live in
  this standalone output directory only. The coordinator owns merging,
  include-path wiring, and any `runtime/c` Makefile/owner changes.
  Nothing was pushed to GitHub and nothing in `~/workspace/fawn-tree`
  was modified.
