# Native WebGPU addon

The N-API addon behind `packages/doe-gpu` ([charter](../../../packages/doe-gpu/CATSCAN.md),
[JavaScript style](../../../packages/doe-gpu/STYLE.md)). It loads one provider
library: the Zig drop-in (`libwebgpu_doe*`) or the C runtime
(`libwebgpu_doe_c.*`). The C rules in [runtime/c/STYLE.md](../../c/STYLE.md)
apply to this C code; this file adds the addon's own invariants.

## Provider-neutral calls

- The selected provider path has an exact byte length; reject embedded NULs,
  truncation and invalid Win32 UTF-8. Windows resolves relative paths once to
  absolute UTF-16 and loads with DLL_LOAD_DIR plus SYSTEM32 dependency search.
  Never repair imports by changing PATH/CWD or falling back to another provider.
  Copy the first native loader operation/code/diagnostic before cleanup. Library
  residency remains on the existing process-lifetime owner, independent of GPU
  resource release. Actual CPU DLL-loader controls do not establish GPU support.
  Open candidates and close rejected/extra references outside the symbol-table
  lock. Named address lookup and table writes remain serialized by that existing
  publication owner; admitted C/Zig exports are ordinary functions, not foreign
  IFUNC resolvers. Do not create a second procedure registry to bypass this owner.
- A `doeNative*` symbol is an optional fast path. When it is absent, call the
  exact webgpu.h procedure the provider exports, or throw. Never fall back to a
  silent no-op, an empty result, or invented success.
- webgpu.h takes some callbacks only at creation. Uncaptured errors and device
  loss are passed in `WGPUDeviceDescriptor`, through a per-device slot
  ([doe_napi_device_events.c](doe_napi_device_events.c)), so that JS
  registration code is the same for both providers.
- A synchronous wait on a standard callback uses `AllowSpontaneous` with the
  atomic handoff in `standard_pop_error_scope`. A provider that delivers late
  frees the capture, so the wait never writes into a dead frame.
- A creator whose provider returns NULL returns `null` to JS instead of
  throwing. The provider has already reported the error to the device's error
  scopes or uncaptured-error callback; JS returns an invalid object, or throws
  when nothing observes errors. The addon never wraps a creation in its own
  error scope, which would hide the error from the caller's scopes.
- Provider errors have one route: the device's uncaptured-error callback,
  registered for the device's lifetime, appends each to the device's log
  ([doe_napi_error_log.h](doe_napi_error_log.h)) and advances the shared
  sequence. JS takes the log in order (`deviceTakeErrors`) and owns every error
  scope; the Node package pushes no native scopes.

## Values and layouts

- Every name-to-value table uses the pinned
  [webgpu.h](../../zig/vendor/webgpu-headers/webgpu.h) value. Prefer the
  header's enumerator name to a literal. The Zig runtime numbers the RGBA16 and
  RGBA32 texture formats differently and starts stencil operations at zero;
  `g_zig_enum_order` translates those at the provider boundary only.
- Leave an omitted optional value omitted. That means `Undefined` for operations
  and aspects, and `WGPU_COPY_STRIDE_UNDEFINED` for strides. Do not default it
  to a value the spec forbids for that format or copy.
- A `WGPU*` typedef in `doe_napi_internal.h` that shares a webgpu.h struct name
  must list the header's members in order. Extensions travel in the chain, as in
  the header, never in members appended to a standard struct. The only appended
  members are those listed in `TRAILING_EXTRAS`, each with its Doe consumer.
- `packages/doe-gpu/scripts/native-abi-layout.js` enforces this on every addon
  build. It checks member lists, `sizeof` and every offset. Mirrors private to
  one `.c` file sit outside the check and must match by hand.

## Checks

- `npm run test:contracts` in `packages/doe-gpu` runs these:
  - `addon-enum-values.test.js`: every numeric table matches webgpu.h, and a
    new table must be registered.
  - `addon-source-size.test.js`: at most 999 lines per file. The two files
    already over it are capped and may only shrink.
- Run behavior changes on both providers:
  - `node test/integration/<file>` on the Zig default;
  - the same with `DOE_WEBGPU_LIB=<stage>/lib/libwebgpu_doe_c.so`;
  - `make -C runtime/c check-js STAGE=<stage>` for the C first-kernel gate.
- Report the adapter each run used.
