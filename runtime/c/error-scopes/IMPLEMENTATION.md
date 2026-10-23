# Error-scope candidate, not accepted

The reviewed ABI-1 contract is implemented by `src/error_scopes.c`. Its private
typed storage is imported through `integration/doe_error_scopes_zig_bridge.h`.
The public header, contract seal, retained Zig stack/tests and original single
classifier remain unchanged. This is a partial stack owner, not full-C native
runtime completion. Production defaults remain retained Zig.

Seven public operations own one mutex, fixed 64-entry/512-byte storage and no heap
allocation. The callback tuple/result/message are snapshotted before unlock, with
no stack access after foreign dispatch. Native integration uses explicit COLD
storage, final-address initialization after backend whole-object assignment,
existing `ReleaseInternal` rollback and checked deinit immediately before free.

## Build and focused checks

From this owner:

- `make BUILD=/absolute/output check` builds the standalone developer C/C++,
  pthread-fault, concurrency and layout checks.
- `make BUILD=/absolute/output native-archive` always recompiles a strict GCC PIC
  archive and creates its source/tool/object/archive receipt.
- `python3 tools/archive.py --archive /absolute/output/libdoe_error_scopes.a`
  refuses source, flags, compiler, archive or member drift. It is local build
  evidence, not an adversarial signing service.

From `runtime/zig`, select only with
`-Dexperimental-c-error-scopes-archive=/absolute/output/libdoe_error_scopes.a`.
Use `test-c-error-scopes-integration` for explicitly initialized CPU fixtures and
`test-c-error-scopes-library` for the test-only native DSO. Its sole test factory,
`doeTestErrorScopesCreateDevice(WGPUInstance *)`, returns one external device
reference and a borrowed real instance. It does not establish GPU/backend success.
No fixture symbol enters ordinary production roots. Retained generic native suites
and browser-WASM reject selection. Main, compute, full and the configured native
artifact closure receive the same archive and bridge; other owner selections stay
independent.

`integration/APPLIED.json` records exact applied native/build/test source identities.
`integration/CONSTRUCTION_AUDIT.json` records the disposition of the 22 frozen
construction leads. Staged copies preserve the earlier handoff layout; the actual
compiled Zig files live under `runtime/zig` and remain charged transitional code.

## Current checks and remaining gates

The executor reset erased the original temporary outputs. Recovery is explicitly
new evidence: the independent executable cases were reconstructed from retained
inputs and all eleven files matched the original seal before candidate inspection.
The original frozen contract/seal remains unchanged.

Fresh standalone qualification passed 65 candidate cases, four terminal-unlock
cases, C++ ABI, 56 shared retained cases, the original 33 tests, 30 classifier
inputs, 15 mutation controls, TSan and ASan/UBSan. LeakSanitizer is unavailable
under ptrace; no universal leak or race-freedom claim is made.

The [combined native recipe](../integration/README.md) freshly builds the selected
owner with C futures/callbacks/instance/labels/routing and real retained Vulkan.
Its C/C++ no-adapter and private-bridge interposition checks pass. A missing pinned
header Make prerequisite was reproduced and fixed. Full selected lifecycle,
package/Node callback ABI, physical GPU and integrated acceptance remain pending.
This code is authored and focused-checked, not production-default promotion or a
50% reduction claim. Historical recovery receipts remain separate from later bytes.
