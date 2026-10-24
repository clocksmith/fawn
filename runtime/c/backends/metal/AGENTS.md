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
Copy terminal NSError text before its autorelease pool drains into the existing
command failure record. Later native observations never replace that first record.

Render passes are whole-call snapshots: validate every draw, then acquire all
leases before creating the native encoder. Attachment clear/load/store state is
speculative until completion, like transfers. Combined depth/stencil aspects use
independent bits in the same subresource owner; resolve targets are separately
leased and published. Never overwrite one aspect while updating another.
Use the canonical attachment initializer to clear missing aspects only; prepare
and preflight the whole selected mip/layer range before opening clear encoders.
Already initialized aspects must LOAD/STORE, and abandonment/unknown completion
must not publish the speculative bits. Logical empty viewport/scissor still encode
the draw so vertex effects and visibility ordering survive. Never use a scissor,
source rewrite or skipped draw to imitate unsupported partial sample masks.
X24/X32 shader views initialize through temporary combined-parent attachment views
over the same selected range. Never attach an X format or discard initialized
companion depth/stencil; reuse the existing speculative aspect and command leases.
Visibility offsets form one contiguous run per pass and share buffer completion
access rules. Timestamp samples share that completion owner too: distinguish stage
and blit capabilities, never resolve unwritten samples, and preserve native error
values instead of fabricating time or assuming Mach clock units. Reset/aggregation and public query-set policy remain upstream.

Portable call-ledger tests are not a Metal emulator, SDK check or physical run.
On an Apple toolchain, run `make sdk-abi` after native ABI record/signature changes;
it compiles the actual arm64/x86_64 SDK targets. Keep `native-check` outside ordinary
`check`; run it only for an authorized healthy Mac, with its 20-second process
timeout and five-second completion deadline. Retain first failures and scope.
Do not install Apple SDKs or silently substitute Vulkan/software execution.
`native-graphics-check` has the same explicit authorization, healthy-host,
20-second process and five-second completion bounds as `native-check`.
Keep the first failing receipt when recording a constrained rerun. No production
selection or public support claim follows from this directory's host controls.


Indirect compute accepts only the typed guard contract: exact shared guard source
and compiler-selected MSL/slots, never unchecked counts. Copy the three GPU words
to private scratch, execute the guard and consume that scratch in native indirect
dispatch. No host argument read or caller mutation. Scratch and both PSOs join the
existing command leases; partial recording failure discards the entire command,
while unknown retains it. Actual num_workgroups comes from Metal's builtin. Keep
canonical source preparation outside frontend locks; actual MTLLibrary/PSO
creation retains this private owner's context serialization.
Indexed/indirect rendering uses the shared common draw source. Preserve the exact
legacy Vulkan bytes separately from the V2 40-byte record with 64-bit capacity
words. GPU index scans use atomic cancellation, signed-base carry/borrow and
strip-only restart markers; never read caller arguments/indices on the host.
Bound packed-index windows to native storage spans without reducing public buffer
limits. Odd uint16 ends require initialized physical padding while logical ranges
stay unchanged. All compute guards precede one whole native render pass. A partial
record failure discards the whole command; unknown retains scratch, PSOs, source
buffers and temporary parameters through the same ordinary leases.
