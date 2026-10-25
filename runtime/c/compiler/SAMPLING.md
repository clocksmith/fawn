# Sampling relationship reflection

[ABI16](include/doe_compiler_sampling.h) adds selected-entry specialization with
complete texture/sampler relationships. It reuses the shared semantic and target
pipeline; there is no source-text resource inference or second compile engine.
Existing ABI1–15 calls and record layouts are unchanged.

Each pair names an entry and two resources in the returned reflection arrays.
Sampling and gather operations, including comparisons, contribute pairs. Loads,
stores and dimension queries alone do not. Transitive helper parameters resolve
through their exact call-site arguments, avoiding a Cartesian product of all
resources seen by a helper. Duplicate relationships are removed in first-use
order. Selected-entry pruning happens before this traversal.

The caller supplies an explicit `sampling_pairs` staging budget and independent
output capacity. Both allocations remain caller-owned. Staging exhaustion reports
pool27 and its next required capacity; undersized output rejects without changing
code, reflection or pair spans. Measure/fill use the same typed traversal. Calls
retain no pointers. Traversal uses the existing bounded walk pool and a bounded
semantic-work format; it cannot recurse on the native stack.

Set `DOE_COMPILER_SAMPLING_METAL_PIPELINE` only when invoking the existing ABI15
fragment-coverage policy. Ordinary Metal compute/vertex specialization uses flags0
and mask `UINT32_MAX`. Non-Metal calls also use those ordinary options. This keeps
explicit fragment absence and sample-mask behavior with their existing owners.

## Developer verification

```sh
make -j1 -C runtime/c/compiler test-sampling
```

The focused test covers all three targets, nested helper formals, disjoint call
sites, duplicate elimination, load-only entries, entry selection, comparison
gather/sampling, Metal fragment coverage, explicit pool exhaustion and atomic
failure publication. `DOE_SAMPLING_ARTIFACT_DIR` optionally writes fresh target
artifacts for independent validators. These controls do not establish SDK,
physical backend or application qualification.
