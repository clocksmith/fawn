# Frozen C drop-in policy owner, ABI 1

Contract frozen; implementation and acceptance are pending. SCOPE.md fixes the
boundary and intentional deltas. The declaration source is include/doe_dropin_policy.h; this file defines its semantics. Review and an
independently sealed case set precede implementation. The initial selected native
qualification is Linux x86_64 with existing GCC/pthreads, not Windows, macOS or
physical-backend qualification. Unsupported selected targets fail explicitly;
the unselected retained build must remain available and unchanged.

## Immutable data and one authority

The C archive contains its own implementation and Python-generated immutable
embedded config. It exports a const-data accessor, not a runtime parser callback.
Standalone callers may instead supply their own correctly formed immutable
config. A consumer supplies one image-lifetime descriptor combining that config,
active native manifest identities and accepted lane identities/aliases. C owns
all state, normalization, precedence, validation and route decisions after this
handoff. No C call invokes Zig, a sidecar, a native proc, or a foreign callback.

The native adapter constructs static descriptors by projecting existing active
manifest groups (compute 69; headless 106; full 114 entries) and LANE_SPECS (18
canonical names plus one alias). Counts are derived, never literals in production
projection code. The manifest supplies the owner explicitly; currently every
active native entry is shared. The lane projection emits each canonical spelling
mapped to itself plus its aliases mapped to that same canonical byte string.
No backend enum crosses this boundary. governed-lanes.json is related governance
evidence, not the generator source for this descriptor.

All descriptor records, arrays and referenced bytes must be naturally aligned,
readable, immutable and valid until the containing provider image is unloaded
after every consumer call has finished. Stack-backed or movable descriptors,
mutation, rebinding and unload with outstanding calls are forbidden. The archive
owns no caller allocation and retains no lookup string or getenv pointer. The
generated arrays live in read-only static storage; zero-length bytes may use NULL.

bind validates the descriptor and records its exact address once. Repeating that
address succeeds; a different address returns ALREADY_BOUND without replacing it,
even if contents compare equal. Before successful binding other stateful APIs
return NOT_BOUND. Binding does not initialize behavior or ownership, read the
environment, or compare ownership rows with the manifest. Basic generated-record
shape validation is not semantic observation of embedded configuration: a
well-formed descriptor may deliberately encode a deferred fatal classification.

## ABI admission and errors

All domain/status/flag fields crossing the header use uint32_t, counts use size_t,
and byte pointers use const uint8_t*. There are no C enum-layout assumptions,
bitfields, packed structs, atomics, synchronization internals, allocator vtables,
function pointers, hidden flexible arrays or Zig slices in the header. The
native adapter maps every owner/mode/tier with exhaustive named switches, not
@intFromEnum casts. A C++17 caller uses the same C layout and extern-C functions.

For each output API, a NULL output returns INVALID_ARGUMENT without observing
configuration/input. Otherwise zero the complete output before validation and
leave it zero on every non-OK return. The output must be writable, disjoint from
inputs/static descriptors and valid through return. The new C API reports invalid
nonempty NULL spans as INVALID_ARGUMENT before lazy initialization. A zero-length
name span is a valid empty name, including NULL/0. Public view admission differs
as specified below. Status, not zeroed fields, decides whether an output is valid.

After output admission/zeroing, owner/route_name validate the input span before
checking binding: an invalid nonempty NULL span returns INVALID_ARGUMENT even
when unbound; an otherwise valid call returns NOT_BOUND when unbound. behavior
has no further call-input check before NOT_BOUND. route_view admits NULL/any
length and non-NULL explicit-zero as API inputs, so an unbound call returns
NOT_BOUND; after binding those inputs initialize behavior as invalid public names.
decide checks output first, then rejects an unknown owner/mode or strict value
other than 0/1 with INVALID_ARGUMENT, regardless of binding; it never observes
or changes process state. No argument error initializes either lazy state.

bind precedence is exact: NULL returns INVALID_ARGUMENT even after binding.
For non-NULL, an already-bound identical address returns OK without re-reading or
revalidating immutable descriptor storage; any other address returns ALREADY_BOUND
without inspecting that alternative descriptor, even if its version/shape is bad.
For an unbound owner, read only the two leading uint32_t fields first; mismatched
ABI version or struct_size != sizeof(the target ABI record) returns ABI_MISMATCH
before nested fields. The caller must supply readable storage for those fields.
Then validate descriptor tier/reserved values; NULL config is INVALID_DESCRIPTOR.
Validate the config version/size before any other config field, then config shape,
manifest shape and lane shape, in that order. Recheck the bound identity under
the cold mutex before any first-bind validation/publication, so a concurrent loser
returns ALREADY_BOUND rather than validating a configuration it will not own.
Unknown admission profile, enum/status code, nonbinary boolean, nonzero reserved field, inconsistent
pointer/count, count multiplication overflow, or invalid name span returns
INVALID_DESCRIPTOR without publishing anything. A fatal behavior descriptor must
encode no environment observation and no lane rows. Reject duplicate decoded
lane_modes keys as INVALID_DESCRIPTOR, including caller-supplied descriptors;
Value parsing already guarantees this for generated data. The generator encodes
malformed/nonobject/wrong-strict fallback as mode dawn, strict 1,
observe_environment 0 and no lane rows. This is not a binding restriction on
every valid no-laneModes configuration: a valid metal default with
observe_environment=0 remains valid. A valid but unconfigured laneModes object
has observe_environment 1 even with zero rows.

Check count <= SIZE_MAX / sizeof(element) before indexing each array. Count zero
permits NULL; nonzero requires a valid array. All nonempty spans require non-NULL
data and lengths representable within the caller's valid object extent; no API
can validate arbitrary pointer readability. Reject manifest duplicate symbols,
empty manifest names and invalid owners. Ownership config may contain duplicate
and empty symbols. Lane spellings/canonical names must be nonempty normalized
ASCII identifiers (a-z, 0-9, underscore), with unique spellings, a self-mapping
canonical row for each canonical target, and no embedded NUL. Reject lengths
above PTRDIFF_MAX or SIZE_MAX-1 where deriving bounded environment scans. These
are descriptor integrity errors, not new JSON schema validation. Manifest/lane
integrity is checked at binding; ownership-vs-manifest agreement is NOT.

Preserve original fatal text through status_message:
- FATAL_DEFAULT_MODE: dropin-abi-behavior.json: unrecognized defaultMode value
- FATAL_LANE_MODE: dropin-abi-behavior.json: unrecognized laneModes value
- FATAL_MANIFEST_CONFLICT: drop-in symbol ownership config disagrees with the proc manifest

C returns and latches semantic fatal status for the affected lazy state. No route
is produced from that failure, and later queries of that state return the same
status without reinitialization. The native adapter maps any non-OK status to
diagnostic termination using C's fixed status message; it must not return shared,
Dawn, NULL-success or retry as a fallback. This is delivery adaptation, not Zig
policy. Exact Zig panic stack/exit-signal formatting is not an ABI promise. API
integrity errors do not poison an otherwise valid bound descriptor.

## Generator admission profile

Inputs are the actual embedded behavior/ownership files, each at most 64 KiB.
Missing/unreadable/oversize input is a build failure as before. Successful input
reading followed by invalid JSON/config does NOT fail generation: emit the
corresponding lazy default/empty/fatal descriptor. A generator execution/OOM/I/O
failure is an infrastructure error and must fail the build without replacing a
previous output. Schemas are tracked dependencies, not substituted admissions.

The parser must preserve ordered object pairs and raw number tokens. Use an
iterative stack, bounded by input bytes, rather than Python's default recursive
JSON decoder/recursion limit. Existing Python standard-library string decoding
may be used with explicit strict UTF-8/scalar validation. Reject BOM, non-JSON
whitespace, trailing bytes, NaN/Infinity extensions, malformed number grammar,
unescaped controls, invalid UTF-8 and unpaired surrogate escapes. Decode valid
surrogate pairs; preserve UTF-8 bytes and embedded escaped NUL with exact lengths.
Do not use Python bool-as-int, binary64 conversion or default last-key-wins.
Large syntactically valid numbers are still valid non-string/nonboolean values.
Unknown fields are syntactically scanned, never ignored as unvalidated bytes.

Behavior interpretation, in order:
1. Parse the entire document as Value semantics: ANY duplicate decoded key in
   ANY object, including unused nested objects, makes it malformed. Malformed or
   nonobject input produces dawn/strict=true/no-environment defaults.
2. defaultMode missing, nonstring or unknown yields dawn initially. Mode tokens
   are exactly the five names in dropin_behavior_policy.zig, ASCII-insensitive;
   whitespace, Unicode case folding and NUL truncation are not accepted.
3. strictFallbackForbidden absent means true; a present nonboolean returns the
   entire defaults immediately, even when a mode string is invalid.
4. If strict=true, an unknown string defaultMode produces FATAL_DEFAULT_MODE.
   Otherwise ANY string-valued laneModes member with an unknown mode produces
   FATAL_LANE_MODE, including unselected or unrecognized lane keys. Nonstring
   members are not fatal. A nonobject laneModes is not fatal. Fatal classification
   must not be observed at generation, binding or a pure route decision.
5. Otherwise a laneModes object permits exactly one environment observation on
   first behavior use. Keep decoded, case-sensitive keys and map nonstring or
   unrecognized values to USE_DEFAULT. Missing/nonobject laneModes observes no
   environment. schemaVersion and unknown fields have no extra semantic effect.

Ownership interpretation:
1. Parse full JSON syntax. Root must be object; required known keys schemaVersion
   and symbols occur exactly once. Each symbols array item must be object with
   required symbol and owner keys each exactly once. Unknown root/item fields,
   including tier/tiers, are ignored semantically. Duplicate unknown fields and
   duplicates inside their skipped objects are admitted, unlike behavior parsing.
2. Profile 1 requires raw unquoted JSON integer token 2 or 3 and string symbol/
   owner. This deliberately rejects quoted/floating versions and byte arrays
   formerly admitted by the typed Zig parser; their lazy result is EMPTY.
   There is no other new numeric policy. In particular numeric values in ignored
   fields remain syntactically valid regardless of magnitude.
3. Owner token parsing is ASCII-insensitive, with no trimming/Unicode folding/NUL
   truncation. Invalid owner or any required-field/type/admission failure makes
   the ENTIRE table empty, never a valid prefix. Preserve array order and decoded
   symbol bytes exactly. Duplicate and empty symbols remain admitted.

Emit numeric byte-array initializers where escaping could be ambiguous; never
derive C length with strlen or accidentally concatenate hexadecimal escapes.
Output and provenance are deterministic for identical inputs/options. No network,
package installation, Zig execution or runtime JSON parse is needed.

## Separate lazy publication and route semantics

The process owner means one singleton per loaded provider image, shared by all
its threads/callers, not thread-local storage or a singleton shared across
unrelated independently loaded providers. Link the private C symbols hidden to
prevent ELF interposition from combining two provider images' policy states.
One private C mutex may serialize cold transitions; separate acquire/release
published behavior and ownership states remain independently initialized.
No reset, reload, explicit teardown, per-device state or alternate policy domain.

Behavior: on first request validate its deferred outcome; if nonfatal, start from
generated default and strict flag. Only when observe_environment=1 call getenv
for FAWN_BACKEND_LANE. Treat unset/empty/unrecognized/too-long as default. Compare
at most maximum descriptor spelling length + 1 bytes. Normalize ASCII uppercase
to lowercase and '-' to '_', with no trim; match canonical or alias spellings,
then look up the CANONICAL case-sensitive laneModes key. Alias-key values do not
override canonical-key values. No matched configured mode means default. Store
only final mode/strict, then publish with release; never retain the environment
pointer. Concurrent setenv/unsetenv/putenv is outside the portable contract.
Changes after publication cannot affect behavior. Environment-copy allocation
failure is eliminated, not replaced by a synthetic fault hook.

Ownership: exact case-sensitive manifest lookup wins without initializing the
JSON-derived table. On the first miss validate ALL ownership rows against the
active manifest, including unused rows and later duplicate rows. Any owner
disagreement latches FATAL_MANIFEST_CONFLICT. Only after complete validation
publish ready. Then choose the first matching config row, otherwise shared.
An ownership-only query does not initialize behavior. A behavior-only query does
not initialize ownership. A config parse rejection is an empty table, not fatal.

route_name evaluates ownership, then behavior, then the pure route rule. This
freezes ownership-first precedence for the previously uncharacterized combined
fatal case. route_view admits NULL with any length or explicit length zero as
invalid: initialize behavior only, use declared/effective shared, effective
strict=false, fallback=false, valid_name=false. A non-NULL sentinel view scans
to NUL and is a valid name even when empty, so it may initialize ownership.
Explicit nonzero views borrow exactly that many bytes, including embedded NUL.
Do not impose the retained sidecar's unrelated 256-byte conversion limit here.
Sentinel callers must provide a readable terminated string; this API cannot
promise a finite independent bound for an unterminated string.

The pure route rule crosses all 5 owners x 5 modes x 2 strict flags: shared and
Dawn are unchanged; strict preserves every owner; otherwise Dawn/mixed modes
reroute the three Doe owners to Dawn, and Doe modes preserve the original owner.
fallback_used is ALWAYS false, even on a reroute. declared_owner is always the
original owner. decide uses valid_name=true without inspecting a name. The real
consumer's last-error writes, original-owner diagnostics, resolved-bit quirks,
sidecar permissions and required/optional caching remain at their current owners.

## Storage, work, threading and failure

Runtime storage is fixed C singleton state plus borrowed immutable arrays. There
are no owner heap allocations, reallocations, copied names, per-call scratch
arrays, retained caller strings or allocator framework. Normalization can compare
bytes directly, so a new fixed maximum lane buffer is unnecessary. No storage
growth occurs after binding. A terminating synchronization failure uses a fixed
diagnostic and aborts; do not return with an uncertain mutex ownership or report
recovered success. Static pthread initialization/lock failures are not confused
with config defaults. No fork-with-live-lock or signal-handler safety is promised.

Let M/O/L/K be manifest/ownership/lane-spelling/lane-mode counts, and B the maximum
lane spelling length. Descriptor checking costs finite pairwise uniqueness work
in M, L and K plus total span bytes. First ownership initialization is O(O*M) exact
comparisons; steady ownership is O(M+O) comparisons, stopping on first match.
Behavior capture is O(L*B + K*B), with environment value reading bounded by B+1.
Exact comparison inspects bytes only after equal-length checks. For a sentinel
name add O(name length) scanning; an explicit name requires no unbounded scan.
No claim of input-independent constant work or total-resolver allocation freedom.
At 64 KiB per JSON input, generated rows/decoded bytes are bounded by input size;
the generator checks output extent/count arithmetic before writing.

The C lock never calls the loader, diagnostics, native procs or foreign code.
The existing loadRequiredProc g_state_lock -> C lock order is permitted; there is
no reverse edge. Release each cold lock before native panic delivery. Atomics
publish only fully initialized immutable results with release/acquire; no copied
non-atomic status may race. Reentrant callbacks cannot occur inside this owner.
Public diagnostic ring and last-error remain process-wide, not thread-local.
