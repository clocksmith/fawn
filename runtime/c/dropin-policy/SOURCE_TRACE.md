# Accepted-source rebind and boundary corrections

Source-only planning at fbd5f079edc8f397eae16adc4240f5b2598e318b. Historical
planning/native-dispatch-owner/SOURCE_TRACE.md and its 42-input manifest remain
unchanged. SOURCE_INPUTS.json rebinds those 42 paths plus 19 newly inspected
dependencies and nine pinned Zig standard-library source files. All 61 repository
paths match accepted HEAD. Of the prior trace's observed bytes, only
docs/c-migration/MIGRATION.md changed by this checkpoint; the runtime/build/config
bytes described in that trace are unchanged. SOURCE_REBIND.json records that
comparison rather than rewriting its historical accepted-HEAD claims.

The observed working tree contains held compiler M11 edits/untracked sources and
root.o. They were not read for this owner or changed. WORKTREE_STATUS.txt is an
observation, not cleanup authority. Overlay had about 24 MiB free at task start;
the package is compact planning text. No build/test/install/cleanup/commit/push
was performed. Existing software-engineering instructions and the applicable
AGENTS, GOALS, INTENT, charter, native C style, architecture, verification,
configuration, Python and artifact-retention owners were read.

## Actual callers and immutable-input provenance

dropin_runtime_config.zig:10-11 obtains both documents only from build_options.
build.zig:462-479 reads their two fixed config paths, with 64 KiB maximum from
line13, and embeds raw bytes without schema enforcement. It is called for
default/selected, compute and full build options. There is no runtime file path,
JSON setter/reload/export, or public API supplying raw JSON to this runtime owner.

A repository search for parse_symbol_ownership_config found only its definition,
the runtime config loader, dropin_symbol_ownership_test.zig and
dropin_proc_manifest_test.zig. Both tests parse the same embedded build-option
file. Actual ownership data is unquoted integer schemaVersion3 and 18 entries
whose symbol/owner fields are JSON strings. The actual behavior document is an
object with ordinary string mode values and strictFallbackForbidden=true.
The private helper accepts a raw slice for internal tests; that does not make its
incidental coercions a published configuration interface. No concrete supported
noncanonical-input dependency was found. This is a bounded source/caller finding,
not proof that no downstream private experiment has ever modified an embedded
file. Selected profile1's deliberate admission delta remains explicit and tested.

The ownership schema admits integer3, not the runtime's version2 compatibility;
the behavior schema is likewise stricter than Value parsing. The design keeps
version2 and all stated malformed/default/fatal/lazy behavior rather than
substituting schema validation. No actual JSON input is changed by this plan.

## Corrected caller path

1. wgpu_dropin_lib.zig:572-603 invokes symbolRouteForView before invalid-view
   rejection/local proc resolution. runtime_config:174-189 distinguishes null,
   explicit0 and sentinel-empty exactly as specified in the contract.
2. symbolOwnerForName at runtime_config:156-164 checks the active native manifest
   first; only a miss reaches ownership parse/publication. That first miss
   validates every config row against active manifest identities (:100-108).
3. Behavior has a different once flag/lock (:110-125). Manifest hits still
   initialize behavior on routing. Invalid views initialize behavior but skip
   ownership. Environment name is FAWN_BACKEND_LANE (:61), observed only if
   laneModes is an object. backend.zig:54-105 owns canonical/alias identities,
   ASCII lower/hyphen normalization and max length40. There are18 canonical
   names and alias vulkan_dawn_directional -> vulkan_dawn_release.
4. dropin_router.zig:9-29 is the complete 50-combination pure route policy.
   Every branch returns fallback_used=false; rerouting is not recorded as
   fallback. Diagnostic owner is independently recomputed from original
   manifest/config in wgpu_dropin_lib.zig:161-174.
5. loadRequiredProc (:189-238) routes while holding the existing loader state
   lock, then tries linked native entries/sidecar according to that decision.
   Its successful typed cache has separate publication. Missing required symbol
   terminates after recording failure/error.
6. CORRECTION: loadOptionalProc (:240-264) has NO route/config call. It ensures
   the sidecar and performs direct library lookup, caching results. Its sole
   found caller is resolveNativeProc's optional sidecar wgpuGetProcAddress
   (:552-570), reached after public routing/local lookup. Adding a route here
   would be an unrequested semantic change, not migration completeness.
7. Actual local resolver bindings/proc implementations, sidecar loader/cache,
   error atomic and ring storage remain outside this owner. The actual C feature
   consumer tests/instance_feature_lookup.c invokes the real loaded DSO and
   checks explicit/sentinel function identity and actual capabilities. The
   manifest unit test exports a stub GetProcAddress and cannot substitute for it.

The original route expression supplies owner/mode/strict as multiple arguments.
This source trace does not independently characterize cross-state fatal ordering.
The candidate declares ownership-first for a valid name and labels that precision
change, instead of claiming a proven old ordering.

## Pinned std.json admission, not Python assumptions

Toolchain source directory supplied by the lead:
/workspace/shared/doe-language-experiment-20261023/phase1/deps/zig-x86_64-linux-0.15.2/lib/std.
The binary was not executed. The source files below are hashed in SOURCE_INPUTS.
Later acceptance must bind actual compiler version/binary identity separately.

- json/Scanner.zig:15-41 documents strict UTF-8, valid Unicode scalars, paired
  surrogate escapes, RFC8259 syntax, ordered duplicate keys and no fixed nesting
  limit apart from allocation. skipValue at252-293 still scans complete syntax,
  including unknown nested values, without rejecting duplicate keys itself.
- json/static.zig:17-55 sets duplicate_field_behavior=error and alloc_if_needed
  for complete slice parsing. parseFromSlice passes full bytes to Scanner and
  parseFromTokenSourceLeaky requires end_of_document, so trailing tokens fail.
- json/dynamic.zig:26-42 interprets numeric tokens as integer/float/number_string,
  preserving oversized integers/nonfinite numeric results rather than rejecting
  valid JSON syntax. No config policy checks a numeric value's exact magnitude.
  Value.jsonParse and handleCompleteValue at83-200 reject decoded duplicate keys
  in ALL objects by default, including unknown nested behavior fields.
- json/static.zig:334-385 rejects duplicate KNOWN typed struct fields, skips
  unknown field values when ignore_unknown_fields=true, and enforces missing
  required fields after parsing. Duplicate ignored keys/nested objects are not
  rejected. The behavior and ownership policies therefore cannot share a
  blanket duplicate-key rejection rule.
- static.zig:239-247 admits numeric and string tokens for integer fields;
  :767-774 uses base10 parseInt or f128 parseFloat followed by integrality/range
  checks. fmt.zig and fmt/parse_float.zig expose the broader string numeric
  grammar. This is NOT Python int/float equivalence.
- static.zig:452-513 admits arrays for []const u8 as well as strings. Array byte
  elements use the same numeric/string integer coercion and u8 range checks.
  Thus ownership symbol/owner can be byte arrays in the retained parser even
  though actual embedded inputs/schema use strings. Profile1's explicit delta
  removes this accidental parser coupling, not an observed consumer contract.
- process.zig:399-422 copies getenv's bytes on POSIX and has separate Windows
  WTF-16/WTF-8 handling. The proposed initial Linux C owner consumes getenv bytes
  without copying and does not claim Windows encoding parity or allocation-fault
  parity for removed allocation sites.

## Scope of these observations

This package establishes source custody, a candidate ownership boundary and
reviewable acceptance obligations. It does not establish compiler invocation,
header layout, C archive execution, generator admission behavior, actual native
consumer success, concurrency, incremental dependency correctness or Debug crash
cause. All those remain UNRUN. Matched contemporary Debug PageAllocator.map
crashes in Z/FCI are observations to retest with bound inputs, never a diagnosis
that moving this policy owner must fix them.
