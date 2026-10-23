# Experimental subgroup extension

This authored extension follows the M13 checkpoint, without changing its frozen
headers, original seal, public layouts or exports. It is focused-tested, not an
integrated or physical execution qualification. Production selection stays Zig.

The shared parser accepts leading `enable subgroups;` and
`requires subgroup_uniformity;`, including repeated names and trailing commas.
Unknown enable/language extensions remain explicitly unsupported. Subgroup use
without the enable directive is a semantic error, including unused declarations
and non-emitted source. Diagnostic filtering is not implemented by this owner.

## Typed operations and interfaces

The shared numeric builtin registry admits scalar and vector i32/u32/f32
`subgroupAdd`, `subgroupMin`, `subgroupMax`, `subgroupMul`, and inclusive/exclusive
Add/Mul scans. `subgroupAnd`, `subgroupOr` and `subgroupXor` admit i32/u32 shapes.
Abstract numeric arguments undergo ordinary concrete default materialization.
All operations remain runtime-stage, even with constant arguments. Ordinary
lookup/shadowing, exact arity, used-result and whole-module validation still apply.

The scalar u32 inputs are `subgroup_size`, `subgroup_invocation_id`,
`num_subgroups` and `subgroup_id`. Their declared/source-static reflection masks
are named in optional `include/doe_compiler_features.h`. Actual emitted entry
interfaces retain the separate target-use mask. The original ABI5 header and
schema layout remain unchanged; callers must not infer new meanings from old
headers or confuse an interface mask with device authorization.

SPIR-V 1.3 emission uses real GroupNonUniform arithmetic with Subgroup scope,
Reduce/InclusiveScan/ExclusiveScan operations and the concrete signed/unsigned/
floating opcode. No host reduction, invocation-order assumption, workgroup
replacement, synthesized barrier or fixed subgroup width is used. Input-only
modules require GroupNonUniform; emitted arithmetic additionally requires
GroupNonUniformArithmetic. Runtime callers must check the selected device's
compute-stage subgroup operations and enabled feature requirements. Successful
`spirv-val --target-env vulkan1.1` is not that check. Other targets must implement
their own verified lowering or reject this extension explicitly.

## Uniformity and bounded ownership

The normative language anchor is
[WGSL CRD 2026-10-21](https://www.w3.org/TR/2026/CRD-WGSL-20261021/), sections
13.3.1.1, 15.2 and 17.12. The target anchor is the
[Khronos SPIR-V specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html),
the version-1.3 GroupNonUniform instructions and capability requirements.

Workgroup and subgroup analyses are distinct sensitivity passes sharing the
existing checked scratch and one bounded operation budget. Subgroup return tags
never establish workgroup barrier uniformity. Reduction return tags differ from
scan tags; the dated argument-return dependency edges remain intact. Subgroup
size and number of subgroups are compute-uniform; subgroup ID is uniform only
at subgroup scope; invocation ID remains varying. Resolved source identities,
skipped expression dependencies and transitive function summaries are retained.

No public capacity, workspace record size or recursive ceiling grows. The
formerly unused private uniformity flags word now records module extensions.
Target planning adds bounded capability bookkeeping. All fallible work remains
before caller publication. Normal/PIC complete graph audits and focused ABI5
reuse tests are developer evidence, not independent acceptance.

## Evidence and remaining scope

`make test-subgroups` checks the overload/target matrix, malformed declarations,
namespace and runtime-stage rules, separate uniformity scopes, source-static
validation, input types/interfaces and ABI5 mask publication. It validates actual
SPIR-V words but does not execute collective instructions. Six unchanged retained
true-subgroup benchmark paths compile and validate; their execution is pending.
Votes, ballot, elect, broadcast, shuffle, quad operations, f16 subgroup values and
diagnostic filtering remain open. No complete compiler migration or final 50%
full-feature size reduction is claimed.
