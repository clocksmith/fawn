# HLSL stage linkage

The shared semantic owner and ABI7 reflection retain source-order interface
leaves, formal/member identities, scalar types, vector widths and interpolation.
The HLSL adapter orders its carrier fields by ascending user location, followed
by builtins in canonical builtin order. Assignments still name the original
logical field IDs. Sorting does not change WGSL return values or reflection.

A native pipeline validates its selected fragment inputs against the selected
vertex outputs, including scalar kind, vector width, interpolation and sampling.
It then compiles the vertex artifact with the compiler-owned
`DOE_HLSL_INTERSTAGE_MASK` definition set to the uint32 mask of fragment-input
locations. The generated vertex carrier declarations and assignments omit only
unconsumed user outputs; position remains. The original entry function still
executes once and computes its complete original return value. Fragment, compute
and vertex-input carriers are not projected.

Without that definition, standalone output retains every declared location.
With the definition, any declared vertex-output location above 31 fails target
compilation explicitly. There is no shift by a high location, silent truncation
or narrower standalone admission. The native profile already validates location
limits before deriving the mask. Zero is the valid position-only projection.
The optional definition is a target compilation parameter, not a WGSL semantic
flag, new reflection layout, source rewrite or shader identity substitution.

Both graphics stages use official DXC Shader Model 6.0, HLSL2021 and explicit
`-pack-prefix-stable`. This policy keeps appended system values from changing the
placement of the identical user prefix. Canonical sorting alone cannot align a
fragment subset with unused lower/interleaved vertex outputs; projection is part
of the contract. Existing compiler output capacity and charged traversal limits
remain unchanged.

Run `make linkage-dxc DXC=/path/to/dxc` in this directory. The control compiles
fresh typed WGSL through this emitter, then compares actual DXIL user semantic
registers, masks and scalar formats. It covers reversed declarations, sparse
locations, float widths 1–4, integer/unsigned values, flat/perspective/linear and
center/centroid/sample interpolation, fragment position presence/absence,
front-facing, full/subset/empty projections, standalone high locations and
explicit high-location rejection. An unprojected wrong-but-typed pair must show
the register mismatch. The owner test also verifies unchanged logical records
and inherited measurement, undersized-output and errno controls.

These are compiler/official-DXC host controls. Windows SDK pipeline construction,
pixels, resource lifetime and physical-device qualification remain separate.
The original public seven-frame graphics consumer is unchanged.

Primary references: [Dawn typed interface sorting and pipeline projection](https://raw.githubusercontent.com/google/dawn/main/src/tint/lang/hlsl/writer/raise/shader_io.cc),
[official DXIL signature contract](https://raw.githubusercontent.com/microsoft/DirectXShaderCompiler/main/docs/DXIL.rst).
The pinned DXC `-help` owns the exact supported packing-option spelling.
