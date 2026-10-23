# Public C native frontend

This owner converts the pinned public WebGPU ABI and orchestrates existing owners.
Do not create a second resource reference count, command store, callback queue,
compiler parser or backend. Device and adapter references belong here; resource
handles alias the resource owner, and instance handles alias the accepted instance.
Never call a foreign callback under a device mutex. A pending backend submission
keeps every resource lease until proven terminal. Unimplemented exports remain
absent; do not generate placeholder success symbols to satisfy a symbol inventory.

Device loss is an independent callback obligation with a weak device identity,
not a second device owner. Clear it before the last external reference drops;
callback delivery may retain storage only while snapshotting under its mutex.
Keep synchronous construction distinct from callback scheduling and pending
queue progress. Before worker publication, acquire process-code residency without
holding device/resource locks. A single loader reference deliberately survives to
process exit; GPU/resource leases settle independently. The pending owner retains
one device lease and polls completion; do not add a second queue or report
unknown work as terminal. Idle final release joins the worker. Preserve the first
background terminal error for a public operation to observe.

Pipeline-layout required slots and usage conflicts belong to the resource owner.
Skip canonical empty slots rather than inventing a native binding requirement.
Carry typed resource/backend allocation and execution failures to the public ABI;
never collapse every failed constructor or submission to Validation.

Query resources own identity/count/recording admission. The Vulkan owner owns
calibration and in-command GPU nanosecond conversion; compile its generated WGSL
before publishing an enabled feature. Never replace this with CPU postprocessing
or raw tick success. Distinguish terminal failure from unknown completion while
preserving typed error status and all pending leases.

Graphics consumes compiler-owned stage/resource/interface reflection and canonical
resource-owned pass/bundle snapshots. Preserve the whole pass when lowering to the
backend, including empty LOAD/CLEAR and ordered occlusion events. Empty vertex
slots differ from explicit Vertex/Instance slots without attributes. Do not invent
buffers or drop historical usage; use the resource owner's required-slot rules.

Depth/stencil initialization belongs to the backend's existing per-aspect command
state. Use real attachment clears before publication and after Discard; never add
frontend availability bits or a fake color attachment. Public Plus format backing
and optional feature identity are distinct. Preserve canonical absent-aspect,
read-only and view-default admission when converting pinned ABI sentinels.

Optional color and fragment stages remain genuinely absent; never synthesize a
shader or attachment. Debug balance belongs to each existing recording owner.
MSAA resolve and discard use the backend's same attachment initialization state.

Backend selection is one tagged payload on the existing device and pending owner.
Never read another backend's union member or introduce a parallel queue/refcount.
Unsupported selections report unavailable without fallback. Metal shaders consume
compiler-owned MSL entry/slot/size metadata; do not parse source or substitute a
SPIR-V artifact. Distinguish chosen frontend limits from queried device and actual
per-pipeline limits. Host native-call ledgers are not GPU execution evidence.

Indirect conversion uses backend-owned generated guards and private scratch on the
existing command owner. Never CPU-read caller arguments or invent a second bounds
engine. Compile the exact guard source, preserve original argument bytes, and
retain unknown work; discard failed terminal helper storage before reuse.

Descriptor-chain presence is ABI conversion, not a second validation engine.
Preserve explicit zero draw budgets and delegate accounting to resource packets.
Platform build recipes must keep one export inventory; never make a missing
platform suite report the success of a narrower host-only substitute.

Surfaces enable WSI only through an explicit compatibleSurface adapter; never
add platforms implicitly or substitute an offscreen image. Present attaches the
frame to a private command object on the existing pending owner and retires it by
the backend presentation fence, never by command completion or queue idle. Take
the surface mutex before any device mutex, and report errors after both unlock.
