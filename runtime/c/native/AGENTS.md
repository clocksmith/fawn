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
holding device/resource locks. POSIX retains one loader reference per provider image;
Win32 uses InitOnce and a permanent FROM_ADDRESS|PIN loader pin, with no ordinary
LoadLibrary fallback. This code residency survives to process exit; GPU/resource
leases settle independently. The pending owner retains
one device lease and polls completion; do not add a second queue or report
unknown work as terminal. Idle final release joins the worker. Preserve the first
background terminal error for a public operation to observe.

Use the private host primitives for mutexes, conditions and real thread handles.
Condition waits hold the recursive device mutex at depth one. Publish the thread
record while holding the same mutex its entry acquires; never detach or free its
storage before startup fields are consumed. Join outside that mutex under the
existing caller lease; self-detach consumes the stored real handle under the
worker lease. Failed initialization must destroy partially live host primitives
before freeing storage; failed join/detach retains its handle obligation.

Error transport is an owned value over the existing native_result mapping, not
another status or recovery engine. Snapshot operation, signed native code and
bounded diagnostic before backend release; preserve the first terminal detail
through background settlement. Format after unlocking, and copy the formatted
message into every deferred pipeline, queue or map delivery. A borrowed stack
message or a successful recovery must not replace the original failure detail.
If callback admission fails after an operation consumed or produced an error,
deliver that error through native_error outside locks before dropping the call's
protecting device/resource lease. Successful untransferred results still release
through their existing owner; never fabricate a completion future or error queue.

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

The advertised host-replay program entrypoint lowers through the same tagged
backend, resource replay references and pending command owner. Keep reusable GPU
recordings Vulkan-only; do not impose that restriction on fresh host replay.
Prepare programs with the resource owner's reversible reservation; publish only
once all native creation, recording and sealing succeeded. Release unpublished
native artifacts before aborting the reservation, and finish rollback before
external error delivery or fallback. A published program remains permanently
claimed. Early program release drops only its own holders; unresolved replay and
native commands retain theirs. Cleanup uses typed release, including Undefined no-op.

Indirect conversion uses backend-owned generated guards and private scratch on the
existing command owner. Never CPU-read caller arguments or invent a second bounds
engine. Compile the exact guard source, preserve original argument bytes, and
retain unknown work; discard failed terminal helper storage before reuse.

Descriptor-chain presence is ABI conversion, not a second validation engine.
Preserve explicit zero draw budgets and delegate accounting to resource packets.
Platform build recipes must keep one export inventory; never make a missing
platform suite report the success of a narrower host-only substitute.

Native shader semantic policy has one owner, native_shader_semantic_flags.
Every target's measure/fill and WGSL feature reporting must consume that policy;
an advertised compiler language capability must have public module, pipeline and
execution controls. A `requires` directive checks selected support and never
enables it. Preserve standalone compiler/shader conservative defaults, and do not
invent a backend feature requirement for compiler-owned raw uniform lowering.

Surfaces enable WSI only through an explicit compatibleSurface adapter; never
add platforms implicitly or substitute an offscreen image. Present attaches the
frame to a private command object on the existing pending owner and retires it by
the backend presentation fence, never by command completion or queue idle. Take
the surface mutex before any device mutex, and report errors after both unlock.
Reserve a typed surface-retirement record before publishing configuration. Arm it
on the same pending list before dropping the last public/frame lease when acquire,
discard, present or caller-held frame state remains unresolved. A command fence
never substitutes for the presentation fence. Worker cleanup keeps the record
linked while dropping the device mutex to acquire the surface mutex; use an
ordinary surface lease for that handoff. Transfer each completed record once and
drop configuration/pending leases outside both locks. Unconfigure has no public
error delivery and therefore must use nonconsuming progress. Cleanup-only surface
records do not delay command WorkDone or create queue errors; genuine unknown
command records still require proven terminal completion.

Public consumer tests select adapters only through tests/adapter_class.h
(adapter_class_options, adapter_class_require) and end PASS lines with
adapter_class_scope(), so one binary runs on the CPU fallback adapter by default
and on a GPU under DOE_C_TEST_ADAPTER=physical. DOE_C_TEST_BACKEND defaults to
Vulkan; explicit Metal selects the native-only Metal adapter and must not claim a
queried integrated/discrete subtype when the provider reports Unknown. Add every
adapter-using client to PHYSICAL_CLIENTS; window-system clients belong to check-present and exit 77 (skip,
never pass) without a display. Never hard-code forceFallbackAdapter or a CPU label.
Adapter selection is by class, and device contexts reopen the adapter's hardware
by UUID; never select or recreate by enumeration ordinal.

Metal compute and render conversion share the selected-entry ABI12 mapping and
one typed resource-snapshot helper. Sparse public group/binding numbers never
become native slots. Validate each row's entry/resource/table/slot identity and
per-entry span; legacy ABI10 rows are not interchangeable. Vertex and fragment
namespaces can differ even for the same public binding. The native buffer-size
table uses the compiler's actual buffer slot; never invent another size index. Allocate vertex-fetch slots around the selected vertex resource/size
slots, preserving canonical empty vertex slots. Whole passes lower once through
the existing backend attachment owner, including empty clear/load and discard.
Never publish a public texture until its real initialization completes; unknown
initialization retains the private allocation on the existing pending owner.
Depth/stencil transfers, indirect/indexed render guards and sample-mask support
must be admitted by their actual composed path, not inferred from another backend.
