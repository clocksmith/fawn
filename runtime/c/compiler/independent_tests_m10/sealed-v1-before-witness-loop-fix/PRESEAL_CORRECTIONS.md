# Pre-inspection evaluator preparation record

All corrections here preceded the concrete source seal, and preceded any
candidate implementation inspection, build, link, load or execution.

1. Fixture generation initially repeated an identical zero/last runtime-tail
   index for one-element ranges. The generator now deduplicates that independent
   index list without changing expected behavior.
2. The handwritten sum of three independently evaluated returned-array values
   was corrected from33 to34 (1+11+22).
3. The target interpreter's first import referred to an arithmetic helper name
   not exported by the immutable M8 numerical oracle. It now calls that oracle's
   existing exact fp_operation entrypoint. No numerical source was modified.
4. Local nominal-member accesses do not require memory-layout decorations;
   resource paths do. The independent target model now distinguishes them.
5. Resource aggregate loads were changed from type-depth Python recursion to
   explicit postorder work items. Sparse local zero/update values never expand
   with the declared element count.
6. The first handwritten value-TypeId mutation control duplicated a u32 target
   type, and spirv-val correctly rejected it. The baseline value family now uses
   i32, independently of the u32 index type. Both the baseline and changed f32
   family are validator-valid and produce distinguishable bytes.
7. Runtime float unary-negation expectations preserve negative zero; Python
   integer -0 is not used as a surrogate for IEEE negative-zero bits.
8. Large target-offset/stride source attributes now use representable u32
   literals whose layout rounding exceeds u32, rather than relying on an
   unrepresentable scalar literal. The uint64 layout-overflow case composes
   representable member sizes and array counts.

Only evaluator code, frozen headers/contracts, immutable accepted historical
oracle/test sources, and the existing public validator were used. The native
helper's separate seal and provenance are preserved unchanged. None of these
preseal repairs is evidence about a candidate.
