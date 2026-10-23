# Metal image author controls, 2026-10-23

Status: authored, strict GCC-compiled and developer-exercised. Not independently
accepted, Metal SDK validated, native-consumed or hardware-qualified. No SDK,
software installation, remote device or physical GPU was used. Production
selection and the shared compiler/runtime were not changed by this owner.

## Implemented boundary

`handles.c` owns native handle types, resource-table validation and bounded typed
call-closure argument propagation. `image.c` owns typed operation decoding,
independent integer guards, native method arguments and result publication.
Existing scalar/control/resource paths call these owners; there is no source
parser, runtime shader substitution, null texture context, or copied writable
texture local. Shared numeric/layout/entry/resource identity remains authoritative.

The seven image operations cover sampled 1D/2D/2D-array/cube/cube-array/3D,
multisampled 2D, admitted depth shapes, and the sixteen admitted storage formats
in 1D/2D/2D-array/3D with read/write/read-write access. Source admission still
rejects meaningless operation/type combinations. MSL handles pixel conversion
and unspecified-channel defaults; host checks do not qualify those conversions.

Buffer slots remain 0..29 and size metadata remains buffer slot 30. Separate
texture slots 0..30 and sampler slots 0..15 use group * 16 + binding, binding <16.
The host must bind those indices and valid WebGPU views. 1D uses the WebGPU
single-level invariant, avoiding the nonexistent MSL explicit-LOD sampling
signature. Integer LOD remains independently guarded before dimension queries.

The obsolete blanket image rejection controls were replaced, not retained as
live behavior. Existing historical `image-guards.json` is unchanged. Test fixture
preparation was moved into one shared test header; duplicated buffer address-space
selection was consolidated. No claims of reduced total source or improved speed
follow from these changes. Per-file/total counts are in the JSON receipt.

## Primary references checked

- Official Apple MSL specification, version 4.1, dated 2026-06-04:
  https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf
  SHA-256: `41538b30d2f1140a5b2a0c84ce0a9f7b67bf0c707e224cfea0bfe5a44aa26cf5`.
  Sections 2.9/2.10 cover texture/sampler types and writable local restrictions;
  section 5.2.1 covers separate argument tables; sections 6.13.1..6.13.14 cover
  dimension-specific methods. Only existing MSL 3.0 baseline methods are emitted.
- Official Apple argument-table baseline:
  https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Render-Ctx/Render-Ctx.html
- WebGPU 1D/single-level rule:
  https://www.w3.org/TR/webgpu/#enumdef-gputexturedimension
- Retained source: `runtime/zig/src/compiler/wgsl/emit/msl/emit_msl_texture.zig`,
  `emit_msl_ir.zig`, `emit_msl_shared.zig`, and `compiler/targets/msl.zig`.
  Their source hashes are retained with the tested C-source hashes.

## Decisive commands and observations

An immutable disposable source copy avoids racing parallel shared-core authors.
The receipt binds the exact target, core and retained-reference source files.
The temporary output directory name used in the raw terminal is not a
qualification verdict; all results in this receipt are author controls only.

- `make check host-control image-host-control library`: strict C11 flags include
  conversion, shadow, missing-prototype and exhaustive-switch warnings as errors.
  Both C tests, scalar/aggregate controls, image operation controls and private
  target archive build passed.
- C image fixtures exercise 192 storage format/dimension/access combinations,
  18 sampled shape/scalar combinations, five depth shapes, helper formals,
  per-entry binding aliases, empty entries, unsupported slots, malformed typed
  identities and atomic no-partial-output failure. Same-size publication is
  checked against measurement; short outputs are left untouched.
- Emitted C++ operation controls pass signed/unsigned coordinate, mip, layer and
  sample edges including negative values, INT_MIN, INT_MAX and unsigned wrap;
  source-order load/store effects; raw floating sampling coordinates/LOD;
  integer layer clamps; 1D base level; distinct-entry resource identity; and
  per-iteration zero reset. Operation spies are not texture/filter emulation.
- Four wrong-but-typed emitted mutants compiled, then failed the corresponding
  checks: inclusive sample bound; layer checked against width; substituted
  sampling coordinate; removed image-result zero reset. These are author test
  sensitivity controls, not an independent WGSL correctness oracle.
- AddressSanitizer plus UndefinedBehaviorSanitizer passed both C executables and
  the emitted image host control with `ASAN_OPTIONS=detect_leaks=0`. Leak checking
  was unavailable under the executor's ptrace environment and is not claimed.
- Repository C/header limit check passed at observation time. The owned source
  maximum is far below 999 physical lines. Native MSL parsing, native binding,
  filtering/format conversions, synchronization and hardware performance remain
  untested here.

## First failures retained

1. The initial strict compile encountered the shared core author's in-flight
   unhandled IR_STAGE_INPUT switch. The shared owner closed that switch; this
   target added explicit new-opcode and graphics-function-flag rejection.
2. An unused-declaration fixture initially used group 9/binding 99. Shared
   admission correctly rejected it before target emission. The fixture now uses
   legal shared group 3/binding 31, outside the target sampler table, to test that
   unused resources do not create native bindings. Shared admission was unchanged.
3. Default LeakSanitizer terminated with its documented ptrace fatal error.
   The same ASan/UBSan executables passed with leak detection disabled. This
   limits the sanitizer claim; it is not recategorized as a successful leak test.
