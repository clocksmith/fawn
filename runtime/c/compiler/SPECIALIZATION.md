# Module validation and pipeline overrides

[ABI13](include/doe_compiler_specialization.h) separates complete module validation
from selected-entry specialization. Earlier ABI1–12 calls retain their layouts,
workspace queries and rejection of `override` declarations.
There is one WGSL parser, declaration graph, numeric owner and set of serializers.

`doe_compiler_validate_module` checks every declaration, initializer type, function
and entry interface. It publishes copied override names/types/IDs and entry
names/stages. Uninitialized overrides, override-dependent expressions and outer
workgroup array extents remain typed, deferred facts. This result contains no
executable code, fabricated value, array layout or workgroup dimension. Const
requirements still apply to resource bindings, structure footprints and other
creation-time attributes. `@id` takes a checked const expression from 0 through 65535;
duplicate IDs and declaration dependency cycles reject.

`doe_compiler_specialize` takes a real AUTO/NAME entry selection and exact key
spans with double values. A declaration with `@id` uses its canonical decimal ID;
other declarations use their names. Unknown/duplicate keys reject. Every supplied
value is converted, including unused declarations: integers must be finite,
integral and in range; f32 must remain finite; bool uses zero/nonzero. Only the
existing admitted concrete scalar types are accepted; this does not add f16.

The complete module first validates symbolically. A bounded traversal then
collects selected functions, workgroup roots and static override dependencies.
Initializer dependencies remain statically accessed even when an outer override
is supplied, so a missing required dependency still rejects. Supplied and
unrequired initializer expressions are not evaluated during specialization.
The same parser reconstructs syntax from the unchanged source into the caller's
workspace. Selected syntax identities are compacted before concrete semantic
analysis; no source text is rewritten and no second evaluator is involved.

The result describes exactly one selected entry at index 0, with its original
declared name/stage and source diagnostics. Resource declaration ordinals remain
unchanged. Selected workgroup counts, byte sizes, initialization and workgroup
dimensions are concrete compiler results; unrelated deferred workgroup types
cannot leak into executable reflection. SPIR-V uses the existing ABI9 records,
Metal the ABI12 dense entry records, and HLSL the ABI11 register records. No target
fallback or additional native specialization policy exists. Existing workgroup
dimensions and their product must fit positive u32 values; device limits remain
a native pipeline obligation. Numeric binding limits follow the explicit semantic flags. Caller capacities and a charged override work limit bound analysis.

Const, override and runtime stages remain distinct after values become known.
For example, `sqrt(n)` can produce a pipeline error for a negative override value,
while `let x=n; sqrt(x)` remains a runtime expression. Original module/source
identity and each independently owned specialization belong to the shader owner;
the allocation-free compiler retains no pointers or handles.

The requirements query appends explicit override records and bounded traversal
state to the selected target workspace. Measurement and publication perform the
same work. All fallible semantic, capacity, target and floating-point restoration
steps finish before bulk output publication. Exact public descriptor/record sizes,
errno preservation and caller rounding/exception restoration apply on failure too.
Internal deferred IR is rejected by every executable target.

[ABI14](include/doe_compiler_module_specialization.h) adds
`doe_compiler_specialize_module` and its matching requirements query. It specializes
all shader entries in their source order, including compute and graphics siblings,
and emits one module or target bundle through the same output owners. Required
functions, workgroup roots and overrides are the union of all entry closures.
Unreachable functions and deferred workgroup roots remain excluded. Every entry
keeps its own interfaces, resource references, workgroup dimensions and storage
bytes; module reflection reports no single selected entry. A module without a
shader entry rejects. Caller records and workspace layouts are the ABI13 records;
ABI13 still accepts only AUTO/NAME selection and rejects NONE.

The retained `--mode vulkan-compute-runtime` spelling does not filter graphics
entries: its preparation pass only conditionally performs compute float fusion.
An actual mixed-stage retained module emits compute, fragment and compute entries
in source order. ABI14 preserves that coverage. It also keeps C's existing strict
required-override validation across all entries: the retained Zig runtime accepted
a missing fragment-only override in this fixture; ABI14 rejects it instead of
inventing an executable value. The focused artifact contains two compute entries
with independent 2/3-element workgroup arrays and a fragment sibling. Both compute
entries execute from the same SPIR-V module on explicit CPU Vulkan and produce
five literal expected words; this is not physical GPU qualification.

Focused host controls:

```sh
make -C runtime/c/compiler test-overrides test-specialization
make -C runtime/c/compiler sanitize-overrides
```

The first control covers whole-module admission, typed deferred values, IDs,
invalid const uses, initializer cycles, untouched outputs and caller FP state.
The second covers all three target adapters, selected compute/graphics closures,
supplied/default/missing/unused values, conversion failures, default evaluation,
exact three- and seven-element workgroup layouts, measure/fill symmetry and failure publication.
Its optional output-directory argument saves the unchanged WGSL and specialized
SPIR-V/MSL/HLSL for external validators and consumers. Host, validator and SDK
checks do not establish GPU semantics, native pipeline adoption or numerical
qualification. Public native end-to-end consumers remain a separate integration
gate; old shader artifacts cannot substitute for a new specialized artifact.
