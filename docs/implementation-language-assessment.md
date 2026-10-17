# Doe: choosing the implementation languages

Zig, C, C++, Rust, Python, JavaScript, TypeScript, Dart, Java, and Kotlin.

Decision report · 17 October 2026 · Prepared for Anthony Robledo.

**Assessment, not a migration decision.** Zig has a defensible role in Doe;
this report does not establish a universally best language. The engineering
judgment is that Rust is the strongest safety-oriented native challenger and
C++ has the closest existing Chromium/Dawn fit. High-level languages deserve
genuine compiler, preparation, and orchestration measurements.

This report separates language capabilities, repository evidence, and engineering
judgment. It contains no controlled implementation-language benchmark, upstream
acceptance commitment, or authorization to rewrite Doe. The current native core
remains Zig. [Architecture](architecture.md#implementation-language-and-native-boundary)
owns implementation boundaries; [GOALS.md](../GOALS.md) owns priorities;
[process law](process.md#language-and-cleanup-evaluation) owns evaluation rules.

## The current job

Doe documents a native WebGPU runtime and independently usable WGSL compiler in
[`runtime/zig`](../runtime/zig/README.md), with JavaScript integration in
[`packages/doe-gpu`](../packages/doe-gpu/README.md). Fawn is a distinct durable
browser-workspace service. The browser wrapper uses the browser's provider;
it does not install Doe beneath Chromium. [Current evidence routing](status.md)
and [runtime status](status/runtime-backends-and-bench.md) distinguish scoped
correctness and historical timings from an accepted current application win.
This review supplies no new performance evidence.

The goals remain unchanged-application wins, page WebGPU ownership inside
Chromium, separately qualified wider GPU paths, and maintainability throughout.
A language must improve those outcomes, not merely simplify a parser demo.

## Shortlist by responsibility

These are candidates to investigate, not measured rankings or a selection of
every candidate for immediate implementation.

| Responsibility | Serious first candidates |
| --- | --- |
| Native GPU objects, queues, lifetimes | Retained Zig; challengers Rust and disciplined C++ |
| WGSL parsing, validation, IR | Rust, Zig, C++; high-level alternatives require compiler measurements |
| Offline planning, conversion, experimentation | Python or TypeScript; Dart, Java, and Kotlin remain eligible |
| Chromium-facing integration | C++ bridge; evaluate supported Rust integration |
| Browser tools, bindings, Fawn workflow code | TypeScript/JavaScript with explicit native boundaries |
| Portable interoperation contract | Versioned C ABI or validated data format, regardless of implementation |

## Native candidates

### Zig: explicit native control, with programmer-owned safety

Explicit allocation, compact data layouts, error unions, `defer`/`errdefer`,
compile-time specialization, and C interoperability fit native handles, compiler
IR, buffer pools, and an embeddable shared library without a managed interpreter.
A small implementation can make allocation and failure paths inspectable.
These are language capabilities; simplicity and performance remain hypotheses.
[Zig language reference](https://ziglang.org/documentation/master/).

Pointer ownership and lifetime correctness remain the programmer's responsibility.
Safety checks do not make Zig memory-safe, and their availability depends on build
configuration. C interoperability is not arbitrary C++ class compatibility.
Measure toolchain pinning, upgrades, debugging, and upstream build integration.
Existing working code reduces migration cost; it does not prove intrinsic
superiority. Doe's [restricted implementation style](../runtime/zig/STYLE.md#restricted-implementation-style)
remains the baseline.

### Rust: the strongest safety-oriented challenger — engineering judgment

Ownership, borrowing, exhaustive enums, and strong types are attractive for
hostile-input parsing and typed IR. Compiler checks catch classes of lifetime
and concurrency mistakes before tests. [Rust ownership](https://doc.rust-lang.org/book/ch04-01-what-is-ownership.html)
and [FFI](https://doc.rust-lang.org/nomicon/ffi.html) support native code and a C ABI
without a garbage-collected runtime. [wgpu and Naga](https://github.com/gfx-rs/wgpu)
provide concrete evidence of Rust implementing native WebGPU and shader translation.

Mutable graphs and cross-language callbacks may require arenas, handles, and
careful ownership design. Unsafe code and FFI retain proof obligations; borrowing
does not establish GPU command completion. [Chromium supports Rust](https://chromium.googlesource.com/chromium/src/+/main/docs/rust/README.md)
through its GN/toolchain conventions and interop machinery, so Rust is not
categorically excluded. Mixed builds, review cost, and acceptance of a particular
integration still need qualification.

### C++: the closest existing browser fit — engineering judgment

[Dawn/Tint](https://dawn.googlesource.com/dawn) and Chromium are the relevant
ecosystem. Compare explicit arenas, RAII, handles, data-oriented IR, and restrained
templates against Zig, not deliberately complicated inheritance. Existing
libraries, debuggers, and [Metal-cpp](https://developer.apple.com/metal/cpp/)
offer integration mechanisms; any reduction in Doe's friction must be measured.

Risks include lifetime and aliasing errors, undefined behavior, complex interfaces,
and expensive builds. RAII closes host scopes; asynchronous GPU destruction still
needs completion-aware retention. A narrow Chromium bridge or reuse of compiler
infrastructure may benefit from C++; a whole-repository rewrite needs a separate
benefit case. Use Doe's [native adapter subset](../runtime/zig/STYLE.md#native-adapter-subset)
for the existing bridge boundary.

### C: a strong boundary, not necessarily the full compiler

C offers direct layouts, mature native integration, and a broadly consumable
interface. The shared [WebGPU C headers](https://github.com/webgpu-native/webgpu-headers)
make this a natural boundary; implementing behind it does not require C.
Versions and implementation extensions must still match.

A full compiler is possible in C, but typed ASTs, containers, cleanup, ownership,
and error propagation require more handwritten discipline. The judgment here is
to compare Rust, Zig, and disciplined C++ first for a large compiler; keep C
eligible for the bounded lifecycle, ABI definitions, and small adapters.

## Dynamic languages: more than wrappers

### Python: credible compiler and orchestration machinery

Python can implement parsing, graph transformations, optimization, code generation,
and substantial runtime orchestration. tinygrad's [compiler layout](https://docs.tinygrad.org/developer/layout/)
and [runtime architecture](https://docs.tinygrad.org/developer/runtime/) are a
counterexample to restricting Python to thin wrappers. Precompiled plans and
cached submissions can move preparation out of repeated execution; Doe must
measure the resulting design rather than inherit tinygrad's claims.

Costs include object-heavy IR, dynamic dispatch, interpreter startup, packaging,
and fine-grained FFI. The [C API](https://docs.python.org/3/c-api/index.html),
[buffer protocol](https://docs.python.org/3/c-api/buffer.html), and
[`ctypes`](https://docs.python.org/3/library/ctypes.html) provide integration,
not automatic elimination of copying, conversions, or callback lifetime problems.
[Free-threaded CPython](https://docs.python.org/3/howto/free-threading-python.html)
changes concurrency options, not the cost of every operation; extension
compatibility matters.

[Cython's typed memoryviews](https://docs.cython.org/en/latest/src/userguide/memoryviews.html)
and compiled hot paths are a separate configuration to measure.
[Nuitka](https://nuitka.net/user-documentation/user-manual.html) changes compilation
and deployment while retaining substantial CPython semantics. Neither warrants
assuming equivalence to handwritten C. The first roles to evaluate are offline
analysis/planning and an inspectable reference compiler pass. Python remains
useful for browser orchestration, evidence analysis, and experimental tooling;
it is not selected to replace the native execution core.

### JavaScript: a fit for the surrounding application stack

JavaScript can implement parsers, planners, and optimizers. Sharing a language
with Doppler and browser tooling could simplify fixtures, debugging, tracing, and
contribution. Typed arrays and batched APIs can keep data paths compact.
[Node-API](https://nodejs.org/api/n-api.html) provides a native-addon boundary
with lifecycle and cleanup facilities.

Measure JIT warmup, allocation/GC, startup, native crossings, and copies on the
intended host. Browser JavaScript's WebGPU calls still use its native provider;
replacing that provider requires the [Chromium integration](../browser/chromium/acceptance.md).
Embedding another JavaScript runtime in a GPU process is a separate decision.
Chromium containing V8 does not make every process or subsystem a free JavaScript
execution environment.

### TypeScript: engineering checks over JavaScript execution

TypeScript is a distinct source-language choice, usually not a separate runtime
candidate: its [type annotations are erased](https://www.typescriptlang.org/docs/handbook/2/basic-types.html#erased-types).
It may make IR structures, manifests, commands, and bindings easier to inspect
and refactor. It does not automatically reduce equivalent JavaScript runtime
work or provide Rust-style ownership. External data and native boundaries still
need runtime validation.

TypeScript is a serious Fawn/tooling candidate if its build and type-check setup
stays simple. A native GPU backend inherits JavaScript deployment questions unless
a different compiler is explicitly selected. Neither TypeScript types nor
JavaScript GC guarantees safe GPU buffer reuse.

### Dart, Java, and Kotlin: eligible, not ranked by this report

Keep these candidates eligible for offline planning, conversion, or experimental
compiler work. No controlled comparison here selects their runtime, deployment
mode, or native GPU integration. Their inclusion in the shortlist does not
authorize three more implementations or establish parity with the native candidates.

## The preparation/execution hypothesis

Let Python or TypeScript express expensive preparation and produce a validated,
versioned plan. A small native core instantiates device objects, submits prepared
work, and enforces completion and lifetime rules. This could improve iteration
without dynamic-language decisions on every dispatch. It could also add another
schema, bridge, and debugging layer. Measure total complexity, including validation,
serialization, ownership, deployment, and invalidation.

The application language, the compiler/runtime implementation language, and WGSL
or emitted GPU code are separate identities. Matching application and implementation
languages does not move arbitrary Python/JavaScript work onto the GPU. A Python
WGSL compiler is not self-hosting merely because it is written in Python.

Use the existing [bounded experiment](architecture.md#language-evaluation-by-layer--proposed-investigation):
one compiler pass and one asynchronous native lifecycle, not a repository rewrite
or a ten-language benchmark campaign. A high-level compiler/preparation reference
uses the same pass and native core. Preserve shaders, independent correctness,
failure behavior, and resource completion. Measure agent implementation time,
build latency, debugging, clarity, integration, binary size, and runtime using the
existing process rules. A migration requires measured gains large enough to repay
migration risk. Until then, this remains a documented assessment and hypothesis.
