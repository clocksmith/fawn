# Shared wrapper descriptors: bounded acceptance

Consumer: acceptance of the package's internal-field descriptor allocation
optimization. The [receipt](c-native-wrapper-descriptors.json) retains 144 timed
samples, exact baseline/candidate source hashes, original output hashes, memory
scope and original run-receipt hashes. Provider/driver/plan hashes and selector
controls are those of the [preceding campaign](c-native-dawn-package-diagnostic.md).
This is diagnostic evidence, not a claim that C beats Dawn.

A fresh-process V8 profile located approximately 5.8 ms in repeated
`hideInternalFields` calls during wrapper class construction. Every prototype
previously allocated getter/setter closures for the same fixed field names.
The accepted candidate lazily constructs a parallel descriptor array once.
Accessors capture only the name: each first assignment still installs a private
non-enumerable data property on its actual receiver. Existing own prototype
properties win; arbitrary custom iterable field names keep their previous path.
The fixed list bounds retained metadata; no native objects are captured.

Independent controls cover instance isolation, deletion/reassignment,
nonenumerability, existing getter/setter preservation, frozen receivers and
custom iterable names. Existing wrapper-internals, full-surface lifecycle and
provider-path tests pass. The real C READ-map mutation-discard control passes.
All 144 timed exact-plan samples had zero error/unsupported/skipped rows.

The earlier Map-based descriptor-cache candidate was slower and discarded.
The array candidate's 40-prototype micro-control improved first invocation
3.43→2.52 ms and warmed median 1.76→1.53 ms. This small diagnostic does not
replace the application measurements below or establish physical GPU behavior.

Same 16+1 canonical package controls, serial blocks baseline C → Dawn →
candidate C, forced identical host-copy mode and GNU-time peak-child RSS:

| Plan | C init ms before → after | C process wall ms before → after | C peak child KiB before → after |
| --- | ---: | ---: | ---: |
| Exact 1 MiB transfer | 73.61 → 67.86 | 247.14 → 227.78 | 101064 → 100788 |
| Eight pipelines | 74.63 → 65.88 | 340.67 → 304.45 | 108556 → 108662 |
| Queue completion | 72.34 → 66.58 | 259.20 → 239.12 | 106514 → 103520 |

The blocks were not randomized. Whole-process changes include scheduling noise;
not all of the wall/operation delta is causally attributed to descriptor reuse.
The pipeline RSS difference is +106 KiB (+0.10%), not a claimed memory win.
The implementation removes repeated closure construction, while preserving all
exercised observable contracts. No native GPU work, validation or oracle changed.

Contemporaneous Dawn still wins operation host timing: C / Dawn transfer
5.76 / 3.62 ms, pipelines 84.34 / 73.49 ms, queue 18.39 / 14.98 ms. These losses
remain explicit. The prior original negatives and frozen failures are unchanged.

Reproduce with the canonical commands and selectors in the preceding campaign,
using its three unchanged plans and `mapAsync-host-copy`; bind the selected
`resource-lifecycle.js` SHA256 from this receipt. Use fresh separate HOME/output
directories, one warmup and 16 timed runs, and retain both baseline and candidate
receipts. No physical GPU, browser, VRAM or universal reliability claim follows.
