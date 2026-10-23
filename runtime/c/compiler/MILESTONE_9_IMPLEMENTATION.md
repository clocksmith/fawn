# M9 implementation candidate: developer evidence, not acceptance

2026-10-23. Frozen contract commit:
`1aa59cd5fe3987a85e8d6348bad4a96326a5cb3c`.
Accepted predecessor: `4e03682849db8101cc8ed35a8243433c305852d5`.
Independent M9 evaluation is pending. Production/default Zig is unchanged.

## Implemented owner

One shared parser represents calls as typed call syntax plus linked argument
roots. All module declarations/signatures precede body resolution. Function
records own statement/expression/IR/local ranges. Parameters remain in their
separate token-derived records; signatures use module lookup, and bodies use
nearest local/formal/module/predeclared lookup. Signature duplicate detection
uses iterative heapsort in the idle target-value slice. No helper/parameter
count-sized automatic storage, library allocation, or call-count host recursion.

Semantics validates every body/return and resolves dead/skipped calls into a
physical-expression callee map. Iterative white/gray/black DFS checks all functions.
Typed argument segments are emission-only, reserved before nested arguments,
filled once left-to-right. Function parameters have semantic IDs beyond the
instruction range. Every helper emits once with reserved target function IDs,
explicit parameters/calls/results/returns and its own Function variables.
Identical function signatures share target types. Helpers access global resources
without entry capture; reflection remains declaration-based. No resource-load
cache or purity assumption crosses calls.

The closed scalar builtin registry owns six identities, arities and overload
families. Constant evaluation uses exact integer ordering and the frozen host
float policy. Runtime math is typed GLSL.std.450; runtime clamp is ordered
min(max(value,low),high), never GLSL Clamp. Mandatory constant bounds are checked
independently of skipped value evaluation. All public API exits preserve errno;
FP restoration still precedes sole word publication.

## Storage and target proof

The frozen formulas are implemented without public-header changes:
F=T/6, P=T/4, A=E, callee slots=physical expressions, I=1+4E+8S (job),
V=I+P+1, S+1 locals/scope and C=E+1 constants. Fixed I remains385, with all
historical accounting exemptions retained. Fixed values898 include512 formals.
Each complete empty function needs6 tokens; each formal needs4 disjoint tokens;
each argument has a distinct charged expression root. Headers/empty bodies and
formals do not consume fictitious E/S capacity. Calls consume E, call statements
S, and value returns S plus their ordinary expression nodes.

Each layout slice checks multiplication/alignment/size. Appends/ranges check
configured capacities before indexing. Instruction semantic IDs are1..I;
formal IDs are I+1..I+P, so the map's last legal index is I+P. Function and
argument identities are checked declaration/expression indices, not target IDs.
B=256+32I+3F+P is computed in uint64 and checked before narrowing; every actual
reservation checks B. Function metadata costs<=3 IDs each, formals1 each,
user calls1 including void, scalar math1, min/max2, clamp4, return0. Existing
M8 operation maxima remain<=32 IDs. Mutable variables remain charged to their
source declaration IR. The fixed bound is14111.

The whole-module conservative word ledger is2048+256I+32F+4P+A. Shared signature
interning and absent helper/formal debug names only reduce that bound. Each
formal costs at most one signature word and three parameter words; each user
argument adds one call word. User calls use four fixed words, min/max11,
clamp22, scalar math6 and return-value2. All staging writes separately check W.
TypeFunction3+P and FunctionCall4+A check the16-bit format limit before operand
serialization; oversized lists are named UNSUPPORTED failures, not word-budget
failures or truncation. The direct job test supplies W=1,000,000 and enough
source/token/expression storage to actually reach the format boundary.

GCC14.2 measurements: workspace119408 bytes/alignment8; core10896; Syntax2784;
IrProgram7928; FunctionRecord80; FormalRecord16; ArgumentSyntax8; Expression40;
ConstantSyntax40; LocalBinding120; IrInstruction48. ABI1 frame119520; maximum
other frame1344 (numeric literal parser). These fit122880/123136/8192 ceilings.
The M8 workspace76512/frame76592 measurements remain unchanged historical facts.

Measured recursive frames: expression192/primary176, statement112/block32;
semantic expression432/call528/mandatory-RHS144/index112/block624; target block256.
For D<=64,C<=32 jobs, conservative paths including rejecting levels are:
parse `(2C+2)*144+(D+1)*368+1024=34448`;
sema `(2C+2)*624+(D+1)*1216+4096=124320`;
emit `(D+C+1)*256+2048=26880` bytes. Nonrecursive leaves/wrappers fit the fixed
margins. ABI1 uses D18/C4, so its largest derived semantic path33440 plus its
119520 wrapper is152960 bytes, excluding caller frames. These are conservative
call-path sums, not observed high-water marks. F/P never increase host recursion.

## Developer checks and preserved failures

Evidence root: `doe-c-migration-evidence-20261023/milestone9-implementation` in
the authorized cloud workspace. Candidate source/artifact manifest is separate.

- Eight new instruction-model tests pass real frames, calls/returns, argument
  side effects/order, fresh locals, repeated/loop calls, shadowing, must_use,
  dead/skipped cycles, returns, builtin stages and signed zero. Every positive
  module passes explicit `spirv-val --target-env vulkan1.1`.
- ASan/UBSan passes the same eight tests, with LeakSanitizer disabled.
- Direct job API checks pass1,000 empty helpers with E=S=0;65,532-formal signature
  success and65,533 failure;65,531-argument call success and65,532 failure. The
  successful large call emits589915 words. Guard bytes and failure output
  atomicity are checked. Math underflow/success and overflow/failure preserve
  a distinct errno sentinel.
- All ten exact frozen witnesses compile through the unchanged C CLI and pass
  Vulkan1.1 validation. This is validation-only witness evidence, not execution
  qualification. Independent execution/oracle and full-corpus gates remain due.
- Existing M8 scalar instruction tests and numeric tests pass. Native boundary,
  control, expression, uniform/layout and exact cap-adapted job/compute/scalar
  API suites pass their original allocation/FP/concurrency/reuse/atomicity checks.
  Existing exact M8 job-lift adapter passes all11 methods. Export checks pass.
- Old tests remain byte unchanged. Raw failures at77760/96208 private byte ceilings
  are classified by exact adapters changing only those assertions to122880.
  The existing M8 exact test_api adapter gains only its trailing empty-helper
  rejection lift; its10,248 checks pass. The existing14-case constant adapter
  passes. Earlier Python rejections for admitted blocks, decimal literals,
  shadowing and constant attributes, plus an inert pre-folding mutation control,
  remain historical failures, not broadly relaxed or claimed as passing. One
  incorrect direct-script Python invocation was rerun via its module path.

First candidate build failure was GCC's maybe-uninitialized alias-ID warning;
explicit zero initialization repaired it. Contract review also corrected the new
format-bound diagnostic category to frozen UNSUPPORTED before format tests.
A new developer overflow fixture initially searched for `nonfinite` while the
stable diagnostic says `not finite`; only that test substring changed to `finite`.
Original failure logs remain. No evaluator fixture has been inspected.

The frozen right-operand float tie policy intentionally aligns the former M8
constant-max discrepancy: M8 source numeric_binary selected its left operand on
equality, while target max selected its right operand. A source-backed executable
probe of baseline numeric.c and current numeric.c records both +/-0 orders at
f32 and AbstractFloat widths; the M9 instruction test checks runtime too. This is
a deterministic policy alignment, not automatically a WGSL conformance defect,
because signed-zero latitude applies. Old contracts/receipts were not rewritten.

The numeric helper's standalone /tmp binaries also report typed-math and injected
FP/errno checks passing, but their source handoff is currently incomplete. Those
logs are explicitly provenance-limited, not a substitute for reproducible native
checks or independent evaluation. No Python/libm self-comparison establishes
accuracy. Physical GPU is unavailable; no runtime adoption, performance, general
WGSL conformance or full migration completion is claimed.
