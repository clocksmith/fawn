# Native WebGPU addon

The N-API addon behind `packages/doe-gpu` ([charter](../../../packages/doe-gpu/CATSCAN.md),
[JavaScript style](../../../packages/doe-gpu/STYLE.md)). It loads one provider
library: the Zig drop-in (`libwebgpu_doe*`) or the C runtime
(`libwebgpu_doe_c.*`). The C rules in [runtime/c/STYLE.md](../../c/STYLE.md)
apply to this C code; this file adds the addon's own invariants.

## Provider-neutral calls

- A `doeNative*` symbol is an optional fast path. When it is absent, call the
  exact webgpu.h procedure the provider exports, or throw. Never fall back to a
  silent no-op, an empty result, or invented success.
- webgpu.h takes some callbacks only at creation. Uncaptured errors and device
  loss are passed in `WGPUDeviceDescriptor`, through a per-device slot
  ([doe_napi_device_events.c](doe_napi_device_events.c)), so that JS
  registration code is the same for both providers.
- A synchronous wait on a standard callback uses `AllowSpontaneous` with the
  atomic handoff in `doe_scoped_validation_end`. A provider that delivers late
  frees the capture, so the wait never writes into a dead frame.

## Values and layouts

- Every name-to-value table uses the pinned
  [webgpu.h](../../zig/vendor/webgpu-headers/webgpu.h) value. Prefer the
  header's enumerator name to a literal. The Zig runtime numbers the RGBA16 and
  RGBA32 texture formats differently and starts stencil operations at zero;
  `g_zig_enum_order` translates those at the provider boundary only.
- Leave an omitted optional value omitted. That means `Undefined` for operations
  and aspects, and `WGPU_COPY_STRIDE_UNDEFINED` for strides. Do not default it
  to a value the spec forbids for that format or copy.
- A local struct mirror (`Standard*`, the `WGPU*` typedefs in
  `doe_napi_internal.h`) must match the header layout field for field.
- `packages/doe-gpu/scripts/native-abi-layout.js` checks the mirrored pass
  descriptors against the header.

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
