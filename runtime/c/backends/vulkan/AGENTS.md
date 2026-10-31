# Vulkan C owner

Follow the common C instructions and style. Keep each declaration's retained or explicitly verified official ABI
source in ABI_PROVENANCE.json and run tools/check_abi.py after ABI changes. A
native error after submission is not proof that commands stopped using resources.
The command owner's existing failure record includes the original operation and
native result, even when the caller declines error output. Proven recovery may
retire uncertain work; it must not rewrite that first failure as queue-idle
success or a generic terminal status. Repeated terminal observations return the
same error snapshot.
Keep physical qualification separate from explicit software ICD tests. Never
modify installed ICD configuration for tests; use process-local selection.

Adapter enumeration order is not identity. Preserve the core device/driver UUID
pair when a native adapter creates a device; reject absent or ambiguous matches
without ordinal substitution. Explicit CPU and non-CPU class selection are
distinct from permitting CPU at an exact ordinal. Query the official ID-properties chain, never names,
vendor/device numbers, pipeline-cache UUIDs or instance-local handles. Keep UUIDs
as owned values and independent contexts under their existing serialization rule.

Adapter-dependent resource policy is one pure rule, doe_vk_resource_profile_select
in device_selection.c, run once from the selected adapter's vendorID and actual
VkPhysicalDeviceMemoryProperties into caps.resource_profile (owner authorized
2026-10-31). DESCRIPTOR_BLOCKS: vendorID 0x1002 (any driver) takes dispatch sets
from reusable FREE_DESCRIPTOR_SET context blocks. DEVICE_LOCAL_HOST_WRITES:
vendorID 0x1002 and the first DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT type's heap
is at least 4 GiB; host-only-written buffers then prefer that type. Every other
adapter keeps the DEFAULT path (one exact pool per dispatch use, host-cached
placement) call for call. Evidence: M7R passed on amd-host RADV (round 5) and on
windows-amd-host; device-local placement passed on the Strix Halo 41.3 GiB UMA
heap and failed on windows-amd-host, whose only such type is a 256 MiB window
(host writes 4.6x slower, no GPU gain). 4 GiB lies between those measured heaps
and equals RADV's maxMemoryAllocationSize; untested sizes stay excluded until
measured. Reports: reports/benchmarks/amd-vulkan/20261029-dropin-phase and
reports/benchmarks/windows-amd-vulkan/20261030-dropin-phase. RADV confirmation of
the landed combined profile is pending. Descriptor and placement code branch only
on these bits, never on names, device IDs or drivers; no second vendor or heap
test may appear elsewhere. Changing the vendor scope or threshold needs frozen
evidence on the affected adapters.

Keep buffer-only and mixed image/sampler dispatches on the same descriptor owner.
Shader type/format/capability admission stays with the compiler/native consumer.
Graphics packets must consume the existing resource render/bundle ordering. A
pass span clears/loads once, records all admitted draws, then stores once, even
when empty. Complete admission, leases and barriers before native begin; old
clear/draw conveniences must use that same recorder. Never invent attachments,
drop unsupported packets or publish speculative layouts. Texture layouts and
initialization and query state publish when `vkQueueSubmit` accepts a recording
(queue order), never at recording; a refused submission poisons what it touched,
and leases stay until the fence proves completion (owner authorized 2026-10-24: async submit A+B, D2, fence-wait completion; futures later; reconfirmed after the section 7 risk review).

Keep viewport convention explicit: retained raw Vulkan defaults remain unchanged;
unmodified WebGPU Position uses one negative-height viewport transform, never a
second compiler flip. Use asymmetric pixels, not full-screen coverage, as evidence.

Timestamp normalization must remain an in-command GPU transform. Preserve the
pinned exact f32-period policy and zero unwritten queries; never relabel raw ticks
or patch CPU readbacks after later commands have already copied them.

Occlusion events belong inside the same typed pass span and existing query lifetime
owner. Validate the whole nesting/index sequence before native begin and reset
written slots outside the pass. Never claim precise counts from flags-zero queries.

Optional device capabilities require explicit selection and successful native
enablement before reporting support. API version or extension discovery alone
cannot admit SPIR-V capabilities. Keep separately verified official declarations
distinct from retained ABI provenance.

Depth/stencil initialization is aspect-specific. Preserve independent load/store,
read-only and discard semantics on the same pass and texture lifetime owners;
never treat cleared depth as initialized stencil or publish abandoned clears.

Public zero-initialization adapters must call the canonical attachment initializer
before exposing discarded contents. Do not duplicate aspect availability in the
frontend or manufacture a color target for depth-only initialization.

Multisample resolve belongs to the same complete pass span. Admit actual format/
usage sample support, retain the resolve view before begin, and preserve its
successful contents independently of the source store/discard operation.

An absent fragment stage is distinct from a malformed shader. Depth-only draws
use the actual depth/stencil attachment and one native pipeline, not a hidden
color target, dummy shader or rasterizer-discard substitute.

Indirect arguments generated by shaders require in-command GPU validation before
native dispatch. Keep guards and scratch on the same command/retirement owner;
never replace them with CPU reads or unchecked native forwarding. Newly admitted
product-sensitive builtins require reflected usage and matching guard semantics.

Indirect graphics guards observe preceding producers and run before the same
render pass, never inside it or hoisted across producers. Use distinct scratch
for every draw, exact bound fetch capacities and overflow-safe comparisons. A
nonzero unsupported firstInstance suppresses the entire draw, not just that field.
Indexed variants share that guard owner and mode word; the native index bind has
no size, so the bound index range is enforced only by direct admission and the
GPU guard. Never read index values on the host.
The argument normalizer source lives in common/doe_draw_guard.h. Keep the Vulkan
wrapper's exact retained bytes/record layout and native capacities unchanged;
sharing that source never establishes another backend's indexed-fetch proof.

Viewport physical bounds and logical requested limits are distinct. An empty
logical viewport still executes vertices and query ordering: normalize only its
native viewport and use an empty scissor, never skip the draw.

Window-system acquisition/presentation has independent native fence obligations.
Never retire it from draw completion or queue idle; preserve unknown ownership and
original errors. Borrow real platform handles, require the complete extension and
feature chain, and keep stale acquired-image generations unusable. No headless or
CPU image may stand in for an unsupported native surface.
