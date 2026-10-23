# Independent candidate1 source and target review

Candidate source is exactly the lead-cleared candidate1 manifest, SHA256
55af8003b0611795585af60ae5f9881ae53d14b1936f6c02223e75f879b85937.
Inspection followed the immutable obligation and executable-oracle seals.
No production edits, developer-test reliance, source inlining or host execution
of runtime WGSL were used to determine expected effects.

- validate.c call_value resolves lexical/formal/input/module scope before builtin
  dispatch, rejects entry calls, checks exact arity/formal types/result-use and
  instruction-format limits, marks the physical expression callee edge even when
  skipped, and reserves the complete outer typed-argument segment only if emit
  is true. Each actual is lowered left-to-right and materialized independently.
- resolve_signatures validates every annotation before reusing annotation union
  storage, checks duplicates with iterative heapsort using caller scratch, and
  has no formal-count automatic array. validate_syntax resets body scope/loop
  state per function. reject_call_cycles uses explicit parent/scan/color records
  over every function, including unused/dead/skipped edges, not C graph recursion.
- spirv.c reserves actual function/signature identities, emits typed parameters
  once, streams signature/call arguments, and emits FunctionCall/ReturnValue or
  Return. Every231 new positive module was independently Vulkan1.1 validated;
  instruction execution includes1024 real activation frames, repeated/nested
  calls, caller continuation, fresh locals, ordered resource effects, snapshots,
  shadowing, robust accesses and helper-only resource use. Every declaration is
  present once; no formal OpName violates the reviewed4P word ledger.
- numeric.c uses one closed six-builtin table. Strict comparison selects the
  right operand on equal min/max (including both signed-zero orders); clamp is
  min(max(x,lo),hi). f32 expf/tanhf/sqrtf and abstract exp/tanh/sqrt are separated
  by volatile declared-width stores. Negative constant sqrt and nonfinite
  evaluated output reject; finite underflow is not rejected merely for ERANGE.
- validate.c always checks constant clamp bounds when both bounds are const,
  including skipped outer operands/runtime values, without charging emitted
  typed arguments during the second non-emitting visit. Runtime participation
  forces concrete overloads and all user calls remain runtime-stage.
- spirv.c emits selected GLSL.std.450 Exp27/Tanh21/Sqrt31 as f32 ExtInst, while
  runtime min/max/clamp use ordered typed comparison/select. No Clamp43/44/45
  occurs in the accepted new modules; handwritten poison-before-select controls
  prove the evaluator would not mistake a later select for safe Clamp execution.
- Numeric expectations are sealed directed Decimal/Fraction enclosures rather
  than a comparison against the same host libm. Binary64 residual fixtures keep
  abstract precision observable before final f32 conversion. Runtime instruction
  modeling uses separately controlled ideal rounding and makes no GPU claim.

The separate native review owns checked storage formulas, concrete record sizes,
actual frames/combined-stack bounds, public atomicity, errno/FP restoration,
no-allocation/import checks, sanitizer/native consumers and format-edge jobs.
No implementation defect has been established by this source review or the
completed semantic/consumer suites. All native/capacity/CLI and combined-stack
checks also closed successfully; their separate receipts preserve first failures
and the exact narrow historical/validator harness corrections.
