# Allocating C compiler host companion

`libdoe_compiler_host` is a separate, versioned host-facing library. It brings
the non-CSL TSIR manifest/source/oracle consumers and the shared typed WGSL→HLSL
→explicit DXC path behind an ordinary C/C++ ABI. It is not a provider switch,
GPU implementation, browser Wasm dependency or full migration certificate.

The public contract is [`doe_compiler_host.h`](../include/doe_compiler_host.h).
Calls borrow inputs synchronously, stage output privately and return one owned
byte buffer. Release that buffer through `doe_compiler_host_release`, including
across CRT boundaries; never free it using a different library's allocator.
Wrong versions/sizes preserve outputs and diagnostics. Occupied result records
reject rather than implicitly losing prior ownership. Underlying status and
source diagnostics remain available when the host status groups errors.

The allocation-free `libdoe_compiler` API, export list, source inventory and
WASI/Windows core builds are unchanged. TSIR and DXC filesystem/process work
belong here, not in the old no-heap library. Both artifacts reuse the same
canonical typed compiler sources; the host adapter has no second parser or
semantic classifier. Reference emitter source files are identity inputs, never
an executable Zig dependency. The retained Zig oracle remains available for
independent differential verification.

```sh
make -C runtime/c/compiler compiler-host -j1
make -C runtime/c/compiler test-compiler-host -j1
make -C runtime/c/compiler test-header-dependencies -j1
make -C runtime/c/compiler test-compiler-host-alloc -j1
make -C runtime/c/compiler test-compiler-host-install -j1
make -C runtime/c/compiler test-compiler-host-dxil -j1
make -C runtime/c/compiler install-compiler-host HOST_PREFIX=/chosen/staging/path
```

Installation is explicit and defaults to checkout-local `build/host-install`.
It stages the header, companion library and standalone C bootstrap tools.
Manifest calls require the repository's named WGSL/reference-source inputs;
moving the library does not embed or silently substitute those inputs. DXIL
calls require an explicit DXC executable and scratch/cache directory, preserve
the real tool's failure and validate its returned container. Native placeholder
emission is not an automatic fallback.

`test-compiler-host` uses separately linked C and C++ consumers, invalid ABI and
owned-result controls, actual source/manifest/oracle calls and symbol-table
checks protecting the old no-allocator library. These are host developer
checks, not GPU/SDK/platform-release qualification. Windows/Darwin execution
must be established on their actual hosts; symbol inspection selects ELF or Mach-O
on the running host. Controlled child tests exercise publication and failure
cleanup through the public DXIL ABI, not actual DXC compilation or GPU execution.

The existing benchmark fixture generator has an explicit C-only consumer route:

```sh
python3 -m bench.tools.generate_tsir_manifest_fixtures --native-only \
  --output-dir runtime/c/compiler/build/native-manifest-entries
```

It requires exactly the nine native kernel/target pairs and never falls back to
Zig. `--check` can compare an explicit generated directory without writing it.
The original twelve-row CSL-inclusive generator and retained independent Zig
oracle remain explicit legacy/reference paths. Historical fixture drift is an
error, not permission to regenerate sealed acceptance inputs.

On an actual x64 Windows SDK developer host, the separate builder reuses the
core source inventory, strict clang-cl flags and canonical host export map:

```powershell
python runtime/c/compiler/tools/build_host_windows.py --out C:\fresh-host-build --dxc C:\dxc\bin\dxc.exe
```

It builds the companion DLL, ordinary C/C++ consumers and C bootstrap CLIs, and
exercises both the controlled-child boundary and actual source-to-DXC call.
It rejects other hosts instead of replacing the SDK/compiler with a stub.
The script's availability is not a successful Windows receipt; Linux focused
checks cannot establish Windows execution. Allocation fault injection uses
GNU linker wrapping and remains a separate Linux developer control.
