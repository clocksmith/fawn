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
Keep synchronous construction distinct from callback scheduling and from the
unfinished asynchronous queue recovery lane.

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
