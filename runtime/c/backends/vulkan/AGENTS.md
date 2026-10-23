# Vulkan C owner

Follow the common C instructions and style. Keep every declaration's retained ABI
source in ABI_PROVENANCE.json and run tools/check_abi.py after ABI changes. A
native error after submission is not proof that commands stopped using resources.
Keep physical qualification separate from explicit software ICD tests. Never
modify installed ICD configuration for tests; use process-local selection.

Keep buffer-only and mixed image/sampler dispatches on the same descriptor owner.
Shader type/format/capability admission stays with the compiler/native consumer.
