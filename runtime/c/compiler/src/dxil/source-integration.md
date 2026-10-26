# Shared C DXIL source ownership

## Two explicit target paths

Both source translation paths use the canonical C parser, semantic/type owner,
module validation, override declarations and numeric environment. There is no
second WGSL parser or Zig fallback in this directory.

`dxil_translate_toolchain*` uses `dxil_source_hlsl`, the shared module-validation
and selected-entry specialization APIs, and production `targets/hlsl`. It
validates the complete module, selects the first source entry, resolves its
required default overrides, and passes the actual emitted entry metadata to
DXC. The native16 requirement selects shader model 6.2 and
`-enable-16bit-types`; ordinary entries retain model 6.0 and the legacy argv.
This requirement comes from typed metadata, not generated-text scanning.

`dxil_translate*` without a toolchain calls `dxil_analyze`, which projects the
same typed owner into the bounded native `DxilIrModule`. The projection retains
functions, complete statement/switch scopes, graphics IO, aggregate initializer
trees, override IDs/defaults, typed expression categories, packed lowering and
checked index bounds. Constant evaluation and concrete runtime folds call the
existing compiler numeric owner; alias/identity rewrites preserve effectful
subtrees and are not a separate evaluator. The native backend still has retained Zig's
zero/no-store/control-flow placeholders and is not a production shader backend.

The low-level `dxil_hlsl_emit` entry accepts caller-built native IR. Its checked
structural view, stage wrappers, pointer snapshots, atomic/numeric/texture helpers
and array guards serialize the current IR on every invocation. It does not
re-analyze saved WGSL or claim to replace canonical source semantic admission.
Float-pack definitions and finite decimal formatting have one shared owner.

## Lifetimes, diagnostics and limits

Source is borrowed synchronously. Both adapters free temporary compiler
workspaces before returning. The HLSL adapter owns its exact text/reflection
allocations; the native analyzer module and copied source names remain in caller storage.
The native translate wrapper and emitter scratch use bounded heap allocations,
not a multi-megabyte automatic module; allocation failure is explicit.
Generated builtin names may instead refer to immutable static strings. No
pointer into a temporary compiler workspace escapes. Source analysis restores
`errno` and the floating-point environment.

The public allocation-free compiler ABI and broad diagnostic status/text are
unchanged. Private typed cause metadata lets native compatibility diagnostics
retain categories such as unknown identifier/type, duplicate symbol and type
mismatch without parsing diagnostic strings. Source failures precede DXC
configuration checks; the low-level module emitter checks configuration before
module lowering. Native and toolchain wrappers publish bytes and `written`
only after successful emission, capacity checks and structural validation.

Host source is capped at 2 MiB and each compiler workspace at 256 MiB. Only
reported typed pool exhaustion triggers bounded growth. Canonical depth limits
and the explicit native tables in `dxil_ir.h` remain checked admission budgets;
no truncation or dummy output substitutes for exhaustion.

## Process and file boundary

A per-invocation exclusively created directory owns the UTF-8 input/output
paths. Existing cache entries are not reused or removed. Input write/close,
output read/close, maximum output size, container validation and cleanup all
participate in success. Cleanup preserves earlier errors; cleanup failure after
otherwise successful compilation prevents publication.

POSIX drains stdout/stderr concurrently with bounded retention and handles
closed standard descriptors. Windows uses the existing wide-character process
owner and UTF-8 filesystem conversion. Neither platform invokes a shell.

## Checks and qualification

From `runtime/c/compiler`, serialize builds, use `-j1`, and bound execution:

```sh
make test-dxil-source test-dxil-io test-dxil-hlsl-complete -j1
make test-dxil-tsir-partial -j1
make test-dxil-tsir-sanitize PARTIAL_SANITIZERS=undefined -j1
```

The native projection tests cover forward types, global constant trees,
override IDs/default dependencies, for/continuing/switch scopes, graphics IO,
matrix/member assignment types, nested arguments, lexical shadowing, reference
categories, packed operations, runtime/fixed-array bounds and diagnostic causes.
The process tests use controlled child executables, not a real DXC substitute.
Windows-target object compilation establishes compile portability only. A bounded
POSIX child also exercises native translation with a one-MiB stack; this does not
substitute for actual Windows execution.

Real DXC compilation of this full source route, actual Windows execution,
D3D12/device behavior and release qualification require separate platform
receipts. Structural container validation does not certify executable shader
semantics. Retained Zig comparisons must keep actual admission/category/byte
mismatches visible; the shared compiler must not regress to copy a retained
frontend limitation. See [gaps.md](gaps.md) for remaining compatibility limits.
