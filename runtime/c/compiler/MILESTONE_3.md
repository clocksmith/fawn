# C compiler milestone 3: existing emitter-consumer integration

Frozen before implementation on 2026-10-23 after accepted M2 commit
`6efd707af20951900114a5452c45d4342bec8163`. This is a CLI/native-build integration
increment, not broader WGSL semantics, new timing infrastructure or runtime work.

## Observed real consumer and mismatch

The existing `bench/gates/spirv_val_gate.py` function
`emit_spirv_for_wgsl` invokes an explicitly supplied native executable with
`--shader-path INPUT --out OUTPUT`, then checks exit status and a nonempty file.
Its `--discover-wgsl --emit-spirv-bin` command path uses that function on real
repository sources. Source SHA-256 of this unchanged consumer:
`f715496e278e6c2fbdad94f4ce9e2a29b8b67095f933990f2568ec9c6237677b`.

The accepted M2 C CLI additionally requires `--reflection JSON`, so that exact
consumer protocol fails before compilation. Preserve the original binary and
record this failure before making reflection optional. Do not replace the
consumer with a newly invented native test and call it adoption.

The existing gate's successful process exit is NOT full corpus acceptance:
emitter failures are labelled EMIT-SKIP and excluded from its validation-failure
count; unrelated prebuilt modules are also validated. Its subgroup option does
not make every skipped source fatal. Discovery also maps duplicate basenames to
one temporary output name. Preserve the consumer unmodified and disclose these
limitations. Independently audit accepted/rejected source counts, source hashes,
fresh outputs and target validation. Never turn its skip denominator into a
complete compiler pass or attribute old prebuilt artifacts to the new compiler.

## Narrow CLI contract extension

`--shader-path INPUT` and `--out OUTPUT` remain required exactly once.
`--reflection JSON` becomes optional, at most once. Unknown/duplicate options,
missing option values and missing required options still return argument failure.
An explicitly supplied empty reflection path is not the same as omission.

Without reflection, successful execution publishes only the SPIR-V binary. It
is byte-identical to the same source compiled with reflection. Do not create a
default sidecar or leave temporary files. With reflection, preserve the existing
complete reflection and two-file ordinary-failure rollback behavior. Both modes
retain no-clobber publication, existing/symlink destination preservation, checked
path and source limits, original diagnostics, and no publication on failure.
Write/close/link failure cleans only owned temporaries and published artifacts;
do not unlink pre-existing destinations. Neither mode claims durability or a
cross-filesystem crash transaction.

Only the CLI owner, focused tests and documentation may change. The compiler
library sources, public header, frozen M1/M2 contracts and library artifact must
remain unchanged. Preserve the M1/M2 executable/receipt snapshots. Record the
exact prior CLI rejection tests now intentionally admitted rather than editing
historical test outcomes. No source-template compatibility shim, hidden Zig
generation/linking/execution or compiler fallback is permitted.

## Source build versus prebuilt and acceptance

1. Build the CLI/library from source with installed GCC and explicit flags in a
   fresh isolated output directory; no Zig or new tools. Separately exercise a
   copied prebuilt C CLI without rebuilding it. Bind sources/tool versions,
   commands, dependencies and executable hashes. A prebuilt-only pass does not
   establish source-build portability.
2. Invoke the unchanged real consumer function with both artifacts on the two
   unchanged residual source copies and the unchanged neighbor-sum fixture.
   Independently validate each fresh module for Vulkan 1.1, execute emitted
   instructions with the accepted oracle, compare to the reflection-enabled
   path, and prove source/input files are unchanged. These are scoped compiler
   component-consumption results, not existing MSL benchmark/application adoption.
3. Exercise unsupported source, missing executable/input, invalid output paths,
   existing outputs/symlinks, long paths, duplicate/malformed options and both
   one/two-output I/O failure modes. Use independent injected write/close/link
   failures, including second-file rollback when requested. No partial success
   or extra artifacts may be hidden by the real consumer's boolean return.
4. Run the unchanged gate's full discovery command once for observation, with
   all temporary files and its report inside this isolated experiment. Before
   running, bind the 98 discovered sources and 36 prebuilt kernel modules at
   the retained revision. Preserve complete stdout/report and separate emitted
   coverage from prebuilt validation. Unsupported sources stay visible; no
   all-corpus, subgroup, hardware or full-gate adoption claim follows.
5. Retain all M2 library/API checks. Independently test the extended CLI and
   native consumer with sanitizer instrumentation and ordinary C/C++ builds.
   The old omitted-reflection rejection is the only new CLI admission; other
   argument/error cases remain. Make the source-only acceptance suite portable
   with an explicit validator path and fresh output directory.

Budget: one small CLI change, one implementation worker, one independent
evaluator, and the existing consumer kept byte-identical. No new WGSL/target,
benchmark executable, statistics framework, system installation, GPU/backend,
public publication or production provider change. Production replacements stay
zero until complete owner and adopter gates close.
