# Metal target owner

Use the common C guidance; do not fork WGSL parsing, validation or layout here.
This directory consumes the private typed module after whole-module validation.
Keep type/resource/local/value IDs distinct. Emit byte-addressed buffer access
only inside independent bounds guards, and derive offsets/strides from shared
layout records. Do not use native C++ structure layout as WGSL buffer layout.

Keep image handles as resolved resource/formal identities. Writable MSL textures
must pass as parameters, not local copies. Integer coordinates, array layers,
mips and samples need independent guards; sampling coordinates/LOD retain sampler
semantics. Preserve buffer slot 30 and the separate native texture/sampler table
policy in README.md. Compare new image mappings with Apple's actual MSL methods,
not another target's packed coordinates or component-width rules.

Use `make check host-control image-host-control` for quick developer feedback, with output in /tmp.
The host controls erase Metal address spaces and attributes, so they establish
neither MSL validity nor GPU behavior. Keep that limitation explicit. Real Metal
SDK/native consumption and independent numerical qualification remain separate.

Shared read-only value IDs, ordered access-path traversal and runtime-tail metadata
live in `../ir_view.h`. Keep target syntax/address policy outside that leaf;
`../tests/test_ir_view.c` exercises malformed identities independently of emitters.

Emit memory helpers only for selected reachable access roots, transfer types and
read/write modes, including recursive aggregate children. `../closure.h` owns the
read-only bounded call/dependency queries; do not grow a parallel semantic graph.
Workgroup zero initialization also requires whole-root store helpers. Keep target
address spaces and byte-view validation local. All walks charge composed work.
