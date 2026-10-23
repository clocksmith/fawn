# Experimental route owner: author build and handoff

Status: authored candidate. Recovered standalone/native controls and the combined
hybrid source recipe pass their exercised scope; final integrated acceptance is
pending. Special-owner and incremental freshness matrices remain unrun.
The normative [contract](CONTRACT.md) and public header are unchanged. Route-off
keeps retained Zig; no default-provider change, physical qualification, performance
claim, or Debug PageAllocator.map diagnosis follows from this candidate.

## Standalone GCC build

From the repository root, using existing GCC, G++, GNU ar and Python only:

```sh
make -C runtime/c/dropin-policy BUILD=/tmp/doe-dropin-policy-author all
make -C runtime/c/dropin-policy BUILD=/tmp/doe-dropin-policy-author check
python3 runtime/c/dropin-policy/tools/verify_archive.py \
  --archive /tmp/doe-dropin-policy-author/libdoe_dropin_policy.a \
  --repo-root "$PWD" --target x86_64-linux-gnu --profile 1
```

Keep the archive, receipt and generated C together in their original build
location. Make refreshes content-bound settings, regenerates immutable data and
records each compiled object before recording the archive. Native selection
verifies current dependencies and archive bytes before every consuming build.
The receipt is freshness evidence, not an adversarial signing authority.

`check` exercises six fresh-process C API cases, all 50 pure decisions, 48,000
concurrent routes, genuine pthread lock/unlock failure termination, a strict
C++17 caller, C/pinned-WebGPU ABI layout and generator/build failure cases.
`native_consumer` is compiled but not executed by this standalone check.

## Real native checks, only after the host gate opens

Use the already approved Zig 0.15.2 executable for the retained native consumer,
never to generate or compile the C owner. From `runtime/zig`, put all caches and
outputs in private `/tmp` directories:

```sh
"$ZIG" build dropin -Doptimize=Debug \
  -Dexperimental-c-dropin-policy-archive=/tmp/doe-dropin-policy-author/libdoe_dropin_policy.a \
  --cache-dir /tmp/doe-route-native-cache --global-cache-dir /tmp/doe-route-global-cache \
  --prefix /tmp/doe-route-native
"$ZIG" build test-c-dropin-policy-integration -Doptimize=Debug \
  -Dexperimental-c-dropin-policy-archive=/tmp/doe-dropin-policy-author/libdoe_dropin_policy.a \
  --cache-dir /tmp/doe-route-native-cache --global-cache-dir /tmp/doe-route-global-cache \
  --prefix /tmp/doe-route-native
/tmp/doe-dropin-policy-author/native_consumer /tmp/doe-route-native/lib/libwebgpu_doe.so
```

Repeat source-matched Debug and ReleaseFast controls with route absent/present,
retained owners and accepted FCI, then F/FC smoke and compute/headless/full tier
projections. Build/run unchanged `runtime/zig/tests/instance_feature_lookup.c`
against each actual source-built DSO. Preserve failures before repairing and
retain the first stack/input; known matched Debug failures stay unqualified.
The archive option is independent of F/FC/FCI and has no route-off prerequisite.

## Fixture-only diagnostics observation

The retained `dropin_diagnostics.snapshot` returns borrowed slices into a local
record copy. This candidate does not repair or use those slices as evidence.
In private source-bound retained/candidate fixture copies only:

1. Append identical `integration/diagnostics_owned_hook.zig` bytes to each copied
   `src/dropin/dropin_diagnostics.zig`; bind original, appendix and result hashes.
2. Copy `integration/native_policy_probe.zig` into each copied runtime's `tests/`
   as a temporary test root. The existing module build options, native linkage
   and C selection must come from that copy's real build configuration. The
   appendix requires `builtin.is_test`, owns fixed-array copies under the actual
   ring lock, and creates no substitute ring/state.
3. Run the two non-death probe filters separately from the explicit
   `native policy probe: missing required terminates` death filter. Record the
   process outcome and diagnostic text, not a predetermined native signal.
4. Never append the hook to release sources. Audit normal release exports for
   absence of the hook and all private policy symbols.

The author C policy contains no allocator, runtime parser, normalization buffer,
lookup-string retention or environment-pointer retention. Native loading,
diagnostics, errors, FCI lifetimes and backend work remain separate owners.
Allocation-site removal is not a Debug fix or measured latency improvement.
