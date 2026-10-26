# TSIR non-CSL implementation and acceptance scope

The earlier Linux worker mapped 38 Zig TSIR files, including seven excluded
CSL-only files, and authored C owners for 28 files with three partial entries.
The three remaining entries now have actual executable replacements:

- `mod.zig`: `src/tsir/tsir.h` re-exports non-CSL owners; both tools consume it.
- `tsir_bootstrap_manifest_inputs.zig`: standalone C tool reads the real three
  bootstrap WGSL sources, validates through the shared C compiler, adapts typed
  syntax into TSIR, plans WebGPU/MSL/SPIR-V and computes actual identities.
- `tsir_bootstrap_oracle.zig`: standalone C CLI preserves arguments, bounded
  file input, reference interpreter execution, status JSON and failure classes.

See [bootstrap commands, evidence and limits](TSIR_BOOTSTRAP.md) and
[remaining semantic qualifications](src/tsir/gaps.md). Original Zig sources and
all CSL exclusions remain intact. This inventory does not establish universal
runtime parity, GPU performance, production promotion or browser qualification.

The scoped tools now build through `make all` and `tsir-bootstrap-tools`, with
an internal static TSIR archive and shared compiler sources. TSIR and DXIL are
still separate from the production `libdoe_compiler.so` source list; production
integration and all broader migration gates remain independently required.
The pre-existing six TSIR tests pass 388 checks on Mars in this follow-up.

Dot v3 removes the standalone strict-warning blockers and adds direct numeric,
formatting and source semantic differentials. The filename is historical:
remaining API/adapter and production integration gaps are not declared closed.
