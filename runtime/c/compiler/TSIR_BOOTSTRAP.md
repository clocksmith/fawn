# Non-CSL TSIR bootstrap tools

These tools implement the remaining facade, manifest-input and oracle
responsibilities from the three named Zig files. Original Zig sources and the
52-file CSL exclusion remain intact. Production provider selection is unchanged.

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
Module constants/memory roots, switch/continuing and unsupported expressions
reject with diagnostics. These limits do not affect the three bootstrap
fixtures. Broader WGSL equivalence, unrelated interpreter/emitter corners,
production-library integration, GPU execution and browser/release qualification
remain separate gates. This inventory does not establish universal runtime
parity or an advantage over Dawn.
