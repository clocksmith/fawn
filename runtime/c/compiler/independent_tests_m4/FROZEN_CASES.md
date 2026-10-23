# Independent M4 black-box freeze

Frozen 2026-10-23 02:01 UTC before inspecting any changed compiler implementation.
Contract SHA256 e87de34c7167ee97fca5071a3fd58bf8492f16a63e60150aca72f4519b8e182b.
Header SHA256 5bf4f5e5a4e4624c094320ad9ec97119278bc7f6f06b0e47c01e444d42c6dc5b.

Retain all M2 164 admitted fixtures, M3 CLI protocol/fault behavior, and old invalid
cases except individually enumerated now-admitted scalar expressions/lets.
A retained failure's diagnostic category/offset may change only because newly
lexed syntax reaches a later stage; rejection itself remains mandatory.

New positives: unchanged scale and ReLU, renaming/declaration permutation/comments,
u32 +,-,* precedence and left associativity, parentheses and max in both operand
orders, modular overflow, direct invocation x values, f32 arithmetic/subtraction/max,
operation-level RNE with no multiply/add contraction, exact decimal materialization,
local inferred/annotated values, local snapshot after storage mutation, chained locals,
u32 local indices/condition base, sibling local names, nested dominance and early
returns, empty/unequal bindings, x zero/last/first OOB/UINT32_MAX and y/z independence.
Decimal oracle uses integers/Fraction only: 16777217.0/16777219.0 ties,
16777216.9/16777217.1 neighbors, 0.1, 0.00000001, 99999999.9, trailing f;
verify identical words/reflection under every available locale and FE rounding mode.

New negatives: mixed types, local writes/duplicates/use-before-declaration,
self-initializer/branch escape/sibling escape/visible user shadowing including dead
source, max wrong arity/unknown calls/constructors, value named max cannot be called,
spelled f32/u32 annotation cannot bypass a visible same-name value. Inferred values
with contextual names remain admitted. Reject float constant-only +,-,*,max at any
depth including suffixed forms and sharp (16777217.0-16777216.0) case; reject
exponent/hex/.5/5./h/ten digits and unsupported unary/division/rem/bit syntax.

Exact limits: expression 64 nodes accepted/65 rejected, primary depth1,
15 parentheses accepted/16 rejected, binary/max depth16 accepted/17 rejected,
16 counted statements accepted/17 rejected; only exact initial unannotated
let n=PARAM.x is free, annotated/parenthesized/further aliases count;
four nested if levels accepted/five rejected. Account whole function and dead code.
API failure atomicity, ABI, all capacities, source/result/word guards, deterministic
parallel calls, native C/C++ source consumption, malformed/mutated ASan/UBSan
corpus; no heap/global or Zig dependencies. Both CLI modes, I/O faults, exact
unchanged real consumer, unique-path 98-source audit (not full gate main).

Oracle independent controls are frozen before candidate execution: synthetic opcode
programs for FSub, FMul, u32 multiply and max lowering; mutate arithmetic opcode,
max compare/select polarity, decimal constant, local saved-value use, branch edge
and Phi predecessor. Wrong results and premature OOB accesses must be detected,
not merely opcode presence. Vulkan1.1 independently validates every admitted module.
No new GPU/backend/NaN/subnormal/inf qualification beyond retained M1 additions.
