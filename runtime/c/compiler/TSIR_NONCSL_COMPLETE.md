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
[remaining semantic qualifications](src/tsir/gaps.md). The retained Zig oracle remains available; its narrow abstract-admission and
invalid-reduction safety corrections are identified in the linked evidence.
All CSL exclusions remain intact. This inventory does not establish universal
runtime parity, GPU performance, production promotion or browser qualification.

The scoped tools build through `make all` and `tsir-bootstrap-tools`. The new
[allocating host companion](host/README.md), `libdoe_compiler_host`, exposes
source/manifest/oracle and explicit-DXC calls through an external C/C++ ABI.
Allocating TSIR/filesystem work stays out of the allocation-free core
`libdoe_compiler.so` inventory. The benchmark fixture generator has an explicit
`--native-only` C route; the independent retained Zig oracle remains available.
Host integration is not a GPU/browser production-promotion certificate.

Dot v3 removes the standalone strict-warning blockers and adds direct numeric,
formatting and source semantic differentials. The filename is historical:
remaining API/adapter and production integration gaps are not declared closed.

## Current correctness follow-up

Low-level planning now hashes a complete target descriptor, not a reduced
placement-only record. All four non-CSL emitter source identities (including
DXIL) are exposed. Explicit graph traversal removes the silent 512-level family
hint cutoff while retaining the reference's eight-local alias rule.

Schema/IR/descriptor text carries optional exact byte lengths. Frontend owned
copies, canonical JSON, planning, lookup and emission preserve embedded NUL and
nonterminated spans. `test-tsir-text` compares six actual retained Zig slice
identities, not a rewritten serializer. See `src/tsir/gaps.md` for API contracts
and remaining evidence boundaries; the 103-row capability registry does not
turn TSIR-local tests into exhaustive native/browser parity.
