# Embedded configuration and process-once route owner

Status: independently reviewed frozen contract; not implemented or accepted.
Accepted source: fbd5f079edc8f397eae16adc4240f5b2598e318b on
experimental/c-migration-dot-20261023. Independent cases must be sealed before
implementation. No build, fixture campaign, source deletion, installation,
default-provider change, commit or push was performed for this package.

## Selected route

One plain C11 static archive owns manifest-first symbol ownership, complete route
decisions, independent behavior/ownership lazy publication and the first-use
FAWN_BACKEND_LANE observation. Existing Python at build time interprets the two
embedded JSON inputs into readable immutable C records. Malformed/default/fatal
classifications remain data and are observed at their original lazy boundary.
The C archive compiles, links and runs with GCC without Zig or callbacks.

The native consumer passes immutable identity descriptors projected directly
from active dropin_proc_manifest.zig groups and backend.zig LANE_SPECS. These
retained sources continue owning typed native proc completeness and accepted
lane identities. No copied registry, Zig policy callback, hidden Zig generator,
or assumption that governed-lanes.json generates backend.zig is permitted.

Actual wgpuGetProcAddress and loadRequiredProc must select the same C owner.
Correction to the historical trace: loadOptionalProc has NO route call. It loads
the sidecar GetProcAddress after the public path's routing; do not add routing to
it. Diagnostics must continue naming the original owner, not the rerouted owner.

Keep proc implementations/bindings, sidecar loader/cache, diagnostic ring and
last-error storage, instance/device/callback/future lifetimes, and physical
backends unchanged. Route selection is independent of existing C future,
callback and instance selections, default-off and explicitly reversible.

## Deliberate, narrowly reviewed differences

1. Ownership input admission profile 1 accepts schemaVersion only as an unquoted
   JSON integer 2 or 3, and symbol/owner only as JSON strings. The retained typed
   parser also admits quoted/integral-float schema versions and coerced byte
   arrays. No actual embedded input or supported runtime-injection caller uses
   those forms. The selected C path turns them into an EMPTY ownership table
   on the first manifest miss. This is NOT exact std.json parity or early build
   rejection. Original controls and independent old/new cases must remain.
2. Runtime JSON, copied-symbol and environment-copy allocations disappear from
   this owner. Their OOM fallbacks cannot be reproduced when the allocation does
   not exist. This is an eliminated failure site, not a whole-resolver zero-
   allocation claim or an explanation of the matched Debug allocation crash.
3. For a valid nonmanifest name with both fatal configurations, ownership is
   evaluated before behavior. This explicitly defines previously uncharacterized
   cross-state failure precedence; do not claim the source expression proved it.

The lead and independent reviewer accepted these three explicit differences after
checking actual caller and input provenance. Nothing
here changes production selection or grants implementation before the case seal.

## Review entrypoints

- CONTRACT.md and include/doe_dropin_policy.h: decisions, lifetime, failure, ABI.
- SOURCE_TRACE.md and SOURCE_INPUTS.json: exact source and historical rebind.
- FILE_MAP.md: proposed implementation and dependency edges.
- ACCEPTANCE.md: author obligations, not independently authored cases.
- CONTRACT.sha256: installed contract seal, not implementation acceptance.
