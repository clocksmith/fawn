# Metal target owner

Use the common C guidance; do not fork WGSL parsing, validation or layout here.
This directory consumes the private typed module after whole-module validation.
Keep type/resource/local/value IDs distinct. Emit byte-addressed buffer access
only inside independent bounds guards, and derive offsets/strides from shared
layout records. Do not use native C++ structure layout as WGSL buffer layout.

Use `make check host-control` for quick developer feedback, with output in /tmp.
The host controls erase Metal address spaces and attributes, so they establish
neither MSL validity nor GPU behavior. Keep that limitation explicit. Real Metal
SDK/native consumption and independent numerical qualification remain separate.
