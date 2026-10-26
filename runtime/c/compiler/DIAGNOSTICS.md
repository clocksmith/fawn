# Global diagnostic controls and ABI18 notices

The canonical parser admits module-level `diagnostic(severity, rule);` controls
before declarations. `error`, `warning`, `info`, and `off` apply to
`derivative_uniformity` and `subgroup_uniformity`. Matching duplicate global
controls are permitted; conflicting controls reject. An optional trailing comma
is accepted. Well-formed unrecognized single-token or vendor-qualified rules do
not disable another rule. The first unrecognized rule produces one warning for
the implementation's **unrecognized diagnostic rule** trigger; this is not a
promise to enumerate every distinct unknown rule.

Semantics follow [WGSL, 21 October 2026, diagnostics](https://www.w3.org/TR/2026/CRD-WGSL-20261021/#diagnostics).
The retained Zig parser accepted global directives by skipping their contents;
this C implementation validates their grammar and applies the policies rather
than reproducing that no-op. Scoped `@diagnostic` attributes are not implemented
by this change. Retained function-attribute parsing alone did not establish a
working scoped filter, and retained statement parsing has no attribute branch.

## One analysis owner

Unmodified error policies use the existing uniformity fast path. Changed policies
reuse that same analysis for each enabled filterable builtin trigger, preserving
its source location through function and pointer-parameter summaries. Mandatory
barrier requirements are analyzed separately and cannot be filtered. `off`
discards only its named rule; warning/info remain recorded diagnostics, not
aliases for disabling unrelated validation.

All runs share the existing uniformity work budget. Unknown-rule conflict checks
reuse the control parser and charge revisited tokens and compared identifier
bytes against that budget. Large filtered modules may exhaust this explicit
analysis budget; there is no hidden unbounded retry. Policy state lives in the
per-call source context; expression notice markers use existing syntax padding.
The private fixed workspace sizes and ceiling are unchanged.

## Additive transport

`doe_compiler_diagnostics.h` adds ABI18
`doe_compiler_validate_module_diagnostics`. It shares ABI13's validation owner and
workspace requirements, while preserving every ABI1–17 public structure and
entrypoint. Measure returns exact metadata/notice requirements. Fill validates
all capacities before publishing either metadata or notices. Wrong ABI or
record/descriptor sizes leave the result untouched; failure publishes no bulk
outputs. A fatal module retains its existing fatal diagnostic, not a partially
accumulated notice array.

Notices own severity, rule, source byte offset/length, line/column, and message
text. They borrow no source pointers. Successful modules report nonfatal
filterable triggers and the first unknown-rule warning. Older compiler APIs and
the current CLI apply policies but have no nonfatal-record output channel; this
change does **not** claim warning/info stderr reporting for that CLI.

The shader owner stores notices in the immutable module allocation. Its
`doe_shader_get_diagnostics` accessor borrows records while the artifact remains
retained; specialized artifacts retain their source module. Native
`GPUCompilationInfo` projects these records into warning/info messages and pins
the module through the existing callback resource lease. Source mutation and
public shader release do not invalidate an admitted deferred callback.

## Focused controls

- `make -C runtime/c/compiler test-diagnostics`: four severities, distinct rule
  policies, mandatory barriers, helper/pointer source attribution, unknown and
  conflicting controls, exact counts, output atomicity and ABI preflight.
- `make -C runtime/c/shader test`: module notices, all-target specialization,
  source snapshots, retained-module lifetime and allocation-failure rollback.
- Native compilation-diagnostics controls exercise public deferred callbacks,
  caller-source overwrite, early release, location/type/count, and projection
  allocation failure. These host/CPU controls do not qualify physical GPUs.

The compiler/shader diagnostics ASan/UBSan controls use leak detection disabled
on the host where LeakSanitizer cannot run under ptrace. Explicit allocator
controls check their owned allocations; no universal leak qualification is
implied.
