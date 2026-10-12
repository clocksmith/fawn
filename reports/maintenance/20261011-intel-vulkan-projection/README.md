# Intel Vulkan projection interchange and rejected acceleration

The frozen Doppler projection passes exact output checks on physical Intel Vulkan
through ordinary Node Doe and Dawn providers. The native compiler/runtime matrix
establishes unchanged SPIR-V execution through Doe for both selected compilers.
Dawn translates imported SPIR-V again through Tint. Its integer-dot reader gap
leaves Tint → Dawn's packed sequence unsupported.

The accelerated candidate is rejected and removed from production. Native Intel
code contains `dp4a` and the candidate passes independent integer controls, but
the complete-projection cohort does not meet the predeclared material benefit.
No whole-application advantage or transfer to ONNX is established. The supplied
W8A8 model also fails its frozen quality gate; projection correctness cannot waive
that gate. The third requested milestone remains unfulfilled.

## Retained changes

- Derive standard shader-source tags from the pinned public C header. SPIR-V's
  tag is 1; the former value 4 rejected a correct native descriptor.
- Preserve authored entry-point names on SPIR-V interface wrappers and reserve
  generated body names. This fixes Tint-reader reconstruction of `main` as
  `main_1` without changing bindings or arithmetic.
- Let the repo-only emitter receive explicit specialization values through
  `--overrides NAME=VALUE,...` in Vulkan-compute-runtime mode. Boolean values use
  0/1. Invalid, duplicate and nonfinite values and unknown options reject.
- Query integer-dot availability separately from signed and unsigned packed
  acceleration. Enable the advertised Vulkan feature/extension on the logical
  device, and reject importing a DotProduct shader when the feature is disabled.
  The compiler retains its portable lowering; acceleration is not selected.

The native import experiment supplies explicit layouts and specialized shaders.
It does not qualify arbitrary SPIR-V validation, automatic binding reflection or
general provider conformance. Existing robust-buffer requirements remain active.
The fixture's bounds, workgroups, ordering and independently referenced status,
packed activation, accumulator, intermediate rescale and final output are checked.

## Frozen workload and custody

`projection.tar.gz` is the unchanged Doppler export, authenticated against every
manifest payload. It uses synthetic deterministic operands for a real query
projection shape M=1, N=1024, K=640; it contains no complete model weights.
The sequence quantizes and packs F32 activations, performs an integer projection,
then applies two separately rounded F32 rescale passes. Weights are prepacked and
resident; each measured invocation includes recording, submission, completion,
all declared readbacks and CPU byte verification. It excludes model loading,
weight conversion, tokenization and generation.

The host is Intel UHD Graphics (TGL GT2), PCI 8086:9a78, Mesa 24.2.8, Vulkan
1.3.289. The Intel ICD is explicitly selected. Both native runners reject CPU
adapters and require the declared physical vendor. Process maps establish loaded
library identity. A diagnostic Vulkan layer retains actual `vkCreateShaderModule`
bytes and logical-device feature admission. The layer is absent from the measured
candidate cohort. Binary and source hashes are in `manifest.json`.

## Four explicit matrix results

| Compiler → runtime | Portable | Packed | Direct selected compiler product |
| --- | --- | --- | --- |
| Tint → Dawn | Exact outputs pass | Unsupported integer-dot SPIR-V extension | No: Dawn recompiles through Tint |
| Tint → Doe | Exact outputs pass | Exact outputs pass | Yes, both sequences |
| Doe → Dawn | Exact outputs pass | Exact outputs pass | No: Dawn recompiles through Tint |
| Doe → Doe | Exact outputs pass | Exact outputs pass | Yes, both sequences |

`matrix.json` retains all compilation, validation, preparation, execution,
cleanup and actual Vulkan shader-custody observations. Its one warmup and three
timed samples per successful sequence are **diagnostic**, with the capture layer
enabled. They are not a performance comparison or release qualification.

| Compiler → runtime / sequence | Sum of cold CLI process ms | Native preparation ms | Median complete invocation ms |
| --- | ---: | ---: | ---: |
| Tint → Dawn / portable | 55.817 | 37.795 | 1.399 |
| Tint → Dawn / packed | 54.187 | 33.303 | Unsupported |
| Tint → Doe / portable | 55.817 | 1.231 | 2.125 |
| Tint → Doe / packed | 54.187 | 0.941 | 0.827 |
| Doe → Dawn / portable | 4.833 | 26.648 | 0.612 |
| Doe → Dawn / packed | 4.776 | 26.847 | 0.619 |
| Doe → Doe / portable | 4.833 | 0.978 | 0.751 |
| Doe → Doe / packed | 4.776 | 1.192 | 0.832 |

Cold CLI wall time includes startup and file I/O. Native preparation includes
buffer setup, upload and pipeline creation; Dawn's SPIR-V-to-Tint work is inside
that boundary. Driver caching is not controlled for this diagnostic. These rows
therefore cannot attribute a pure compiler speed difference or isolate driver
code generation. They explicitly separate the measured boundaries.

## Rejected packed-dot experiment

The opt-in candidate matched typed packed-byte operations and emitted signed or
unsigned native dot instructions only under explicit target availability and
acceleration inputs. No model-specific compiler rules were introduced. Both
independent integer-family and single-evaluation controls validate and pass on
physical Intel. Actual driver code contains `dp4a`; the portable diagnostic has
no `dp4a` matches. Driver dumps are diagnostic and outside measured invocations.

`policy.json` was frozen before execution: 12 balanced alternating process pairs,
64 timed invocations and 12 warmups per child, primary ratio at most 0.90 and
regression ratios at most 1.05. Every one of the 24 children is retained. Median
paired complete-projection ratio is 1.009801; preparation 0.998030, invocation CPU
1.009250, peak RSS 1.0. **The candidate fails the primary requirement.** Variability
and absent A/A calibration further prevent any speed or tail claim. The result
does not establish why the instruction-level change lacks a complete-operation
benefit.

No Chrome processes were present at measurement. Before/after DRM observations
find no advancing readable foreign GPU client. Unreadable processes remain in
the receipt, so these observations do not prove exclusive host GPU access. No
competing agent process was stopped. This limitation cannot strengthen admission.

`cohort.json` and the raw child observations permit recomputing the frozen
rejection. `rejected-candidate.patch.gz` is the complete candidate delta against
`sourceBase`, including the interchange fixes later retained; it is not an
optimization-only patch. The retained source hashes, candidate SPIR-V, diagnostic
logs and binary identities preserve the unsuccessful experiment. The candidate
binary itself is local and is not distributed in this report.

## Model boundary and remaining milestone

The supplied Doppler qualification reports W8A8 CPU quality 5/12 against a 10/12
gate, including loss of an F16-passing task. Its installed GPU evidence also
does not qualify model quality. The original quality README and compressed
source receipts are retained inside `raw-evidence.tar.gz`; the manifest binds
their source identity. No quality threshold, numerical policy or model artifact
was changed. Complete generation, cancellation/reuse performance and unrelated
application transfer require a qualified model and a retained mechanism with a
measured advantage; neither is established by this campaign.

## Acceptance and reproduction

The final source passes 1,163 WGSL tests and 2,358 runtime tests, with 11 runtime
skips. Import-fence and source-layout checks pass. The public Node native-render
ownership and shader-semantics checks pass on this physical Intel Vulkan host.
Both ordinary Node projection controls match all independent expected bytes.
Five malformed CLI controls reject. Six adversarial evidence controls reject
tampered custody, output, child accounting, policy identity and promotion.

Schema and CATSCAN gates pass. Links in the changed Markdown resolve. The global
documentation-link test still fails on 26 pre-existing missing local
`bench/out/compute-program/` artifacts; its failure is retained and does not
qualify those unrelated historical runs.

```sh
python3 bench/executors/validate-projection-evidence.py reports/maintenance/20261011-intel-vulkan-projection
python3 -m unittest bench.tests.test_projection_evidence -v
python3 bench/gates/schema_gate.py
python3 bench/gates/catscan_gate.py
```

For a new execution, extract `projection.tar.gz` to a local fixture directory and
run `bench/executors/run-projection-controls.mjs` with an explicit provider,
library, unused output directory and `intel`. For the matrix, build the source
emitter and drop-in library using the pinned Zig, compile `projection-native.c`
against each provider's own pinned header, and build the custody layer as a
shared object. `run-projection-matrix.py --help` names every required tool input.
Set `VK_ICD_FILENAMES` to the Intel ICD. It creates fresh manifest-derived plans;
historical absolute plan paths in raw evidence are provenance, not portable input.
The measured candidate cohort requires the exact rejected candidate and frozen
policy; do not reinterpret its result as an accepted-source benchmark.

Component: WGSL compiler, native Vulkan backend and repo-only projection evidence.
Intent: preserved.
Acceptance evidence: commands above; manifest, matrix, Node controls and raw bundle.
Boundary effects: standard C shader import, SPIR-V names, emitter specialization,
and Vulkan feature admission; Doppler model policy remains with Doppler.
