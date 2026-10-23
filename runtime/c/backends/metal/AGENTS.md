# Metal C owner

Follow the common C guide and this owner's [style](STYLE.md). Keep ordinary
WebGPU validation/reference ownership in the shared resource/native owners.
Never call a borrowed native handle outside the command lifetime protocol.
The actual Metal command buffers have unretained references: recording leases
must exist before native encoding and survive pending/unknown completion.
Textures, parent-backed views and samplers participate in that same owner. Array
slices and 3D origins are distinct native coordinates, not interchangeable depth.

The compiler supplies validated MSL, resolved workgroup size, flattened slots and
optional sizes slot for each stage. Render inputs include explicit vertex/fragment
symbols, vertex layouts and native render state. Never guess these from shader
text or from pipeline maxima.
Keep the exact C method signature for each Objective-C selector. Review both
x86_64 struct-return and arm64 dispatch branches after ABI edits. Native error
codes and original operation names must survive failed encoding and completion.

Render passes are whole-call snapshots: validate every draw, then acquire all
leases before creating the native encoder. Attachment clear/load/store state is
speculative until completion, like transfers. Combined depth/stencil aspects use
independent bits in the same subresource owner; resolve targets are separately
leased and published. Never overwrite one aspect while updating another.
Visibility offsets form one contiguous run per pass and share buffer completion
access rules. Timestamp samples share that completion owner too: distinguish stage
and blit capabilities, never resolve unwritten samples, and preserve native error
values instead of fabricating time or assuming Mach clock units. Reset/aggregation and public query-set policy remain upstream.

Portable call-ledger tests are not a Metal emulator, SDK check or physical run.
Do not install Apple SDKs or silently substitute Vulkan/software execution.
Keep the first failing receipt when recording a constrained rerun. No production
selection or public support claim follows from this directory's host controls.
