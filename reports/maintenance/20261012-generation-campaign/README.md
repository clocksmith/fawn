# Complete-generation campaign preparation

The [v2 policy](../../../bench/external-projects/doppler-generation/complete-generation-v2.json)
freezes unchanged Q4 Gemma inputs and complete-generation acceptance before
physical measurement. It preserves historical candidates and verdicts.
The [correctness repair](../20261012-glsl-instruction-mappings/README.md) is the
required starting point; old broken bytes cannot be used as a speed baseline.

`reference.json` and `reference.log.gz` were produced by the existing independent
CPU oracle using the exact deployed tensors and tokenizer. They bind the new
contract and oracle source digest, and retain library versions and greedy token
margins. Prompt/output token counts are 42/192, 892/9 and 18/17. The story stops
at its declared token budget; the other two cases reach EOS. This establishes
expected computation and stopping, not conversational task quality or physical
provider qualification. W8A8 is excluded.

Reproduce from repository root, with the recorded CPU library versions available:

```sh
python3 bench/external-projects/doppler-generation/reference.py \
  --contract bench/external-projects/doppler-generation/complete-generation-v2.json \
  --out "$NEW_REFERENCE_FILE"
```

## Remaining execution work

The AMD window remains unconfirmed. No new physical readback, Doppler baseline,
A/A control, candidate, or ONNX transfer was executed. Chrome GPU activity is
retained in local preflight custody under
`bench/out/doppler-generation/20261012-vulkan-campaign/preflight.json`.

Before timing: verify shader readback; bind corrected library, installed package
and provider artifacts; qualify all cases, cancellation, reuse and settlement.
Extend the existing cohort and analysis owners for independent-process A/A labels
and all three cases. The old single-case timing and promotion paths deliberately
reject this policy, with a negative regression test. Policy existence is not
executable performance admission.

Attribution still needs invocation counts, GPU shader-family costs, generated-code
differences, critical-path contribution and instrumentation controls. Source
inspection shows that immediate upload flushes can use the fence helper while
deferred completion can drain a timeline semaphore. The presence of the polling
helper does not establish which branch this application executes or its cost.
No optimization mechanism is selected and no speed advantage is claimed.
