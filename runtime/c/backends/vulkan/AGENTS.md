# Vulkan C owner

Follow the common C instructions and style. Keep every declaration's retained ABI
source in ABI_PROVENANCE.json and run tools/check_abi.py after ABI changes. A
native error after submission is not proof that commands stopped using resources.
Keep physical qualification separate from explicit software ICD tests. Never
modify installed ICD configuration for tests; use process-local selection.

Keep buffer-only and mixed image/sampler dispatches on the same descriptor owner.
Shader type/format/capability admission stays with the compiler/native consumer.
Graphics packets must consume the existing resource render/bundle ordering. A
clear-only begin initializes the selected subresource; later draws load it. Never
invent attachments, drop unsupported packets or publish speculative layouts.

Keep viewport convention explicit: retained raw Vulkan defaults remain unchanged;
unmodified WebGPU Position uses one negative-height viewport transform, never a
second compiler flip. Use asymmetric pixels, not full-screen coverage, as evidence.
