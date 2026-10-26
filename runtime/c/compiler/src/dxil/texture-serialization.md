# Direct-IR HLSL texture serialization

`dxil_hlsl_texture.c` serializes the caller-built `DxilIrModule` texture
operations through the existing HLSL writer. It is not a parser or an alternate
semantic pipeline. The production WGSL source route keeps the shared compiler's
admission, effects and target owners.

The dispatcher covers all 14 names in the retained
`runtime/zig/src/compiler/wgsl/emit/hlsl/emit_hlsl_texture.zig`: load, store,
sample, explicit level, compare, compare level zero, gather, gather compare,
gradients, sample offset, level offset, dimensions, mip levels and array layers.
Metadata helpers use the bound resource identity and its actual dimensionality.
Sampling uses the declared regular/comparison sampler kind. Gather components
0–3 select the corresponding HLSL channel rather than silently always using red.

Bound 1D/2D loads and float storage-2D loads/stores call typed helpers. Coordinates,
mip levels and stored values occur once in the call, not repeatedly inside the
bounds condition. Negative coordinates, excessive coordinates and invalid sampled
mip levels return zero or suppress the store. Read-only storage textures use
`Load(int3(coord, 0))`; writable storage textures use typed indexing. Bound helper
names never stand in for unbound resource parameters.

The compatibility IR retains narrower coverage than the shared compiler:
multisampled loads, sampled 3D/array loads, array-sampling argument packing and
integer storage texture operations are not admitted here. Wrong arity, shape,
sampler kind, result type or malformed argument spans fail rather than publishing
successful invalid text. No new source fallback is used. Offset-expression
constant legality and stage restrictions remain the shared semantic/DXC owner's
responsibility, not a new evaluator inside this serializer.

`make -C runtime/c/compiler test-dxil-hlsl-texture` exercises caller-built IR for
all names, dimensionality/register identity, comparison samplers, all gather
channels, guarded load/store output, once-emitted call operands, malformed spans,
unsupported forms and writer-capacity canaries. These host serialization controls
and sanitizer runs do **not** establish actual DXC acceptance or physical D3D12
execution; those still require the Windows target lane.
