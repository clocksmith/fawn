# Floating decomposition builtins

The shared C semantic owner admits `frexp`, `modf` and `ldexp` for scalar and
vector abstract floats, f32 and enabled f16. Implicit two-field result types keep
`fract` plus `exp`/`whole` identities; they are not user-spellable structure names.
Their children, constant materialization and target declarations use the existing
typed graph. Floating exponent fields are not substituted for WGSL integer fields.

`ldexp` resolves its floating input and signed integer exponent independently,
then enforces their equal vector shape and abstract/concrete conversion rules.
A constant or specialized override exponent above the floating bias plus one
rejects even when the floating input is runtime. Runtime out-of-domain values
retain WGSL's indeterminate-value contract rather than creating a false error.

Constant evaluation reuses the numeric owner's finite conversions and binary16
packing. Runtime SPIR-V uses the standard GLSL extended instructions. MSL and HLSL
use typed result helpers around their native decomposition intrinsics; HLSL's
floating exponent output is explicitly converted to the required signed integer.
These mappings are authored C/text emission, not native SDK qualification.

```sh
make -j1 -C runtime/c/compiler test-decompose SPIRV_VAL=/path/to/spirv-val
```

The developer control covers f32/f16 scalar and vector widths, abstract constant
result materialization, arrays of result structures, exact constant payloads,
three target emissions, fresh SPIR-V validation, wrong member/type/shape errors,
and shader/pipeline exponent bounds. Physical-backend and retained independent
application qualification remain separate.

Normative contracts: [WGSL numeric builtins](https://www.w3.org/TR/WGSL/#numeric-builtin-functions)
and [GLSL extended SPIR-V instructions](https://registry.khronos.org/SPIR-V/specs/unified1/GLSL.std.450.html).
