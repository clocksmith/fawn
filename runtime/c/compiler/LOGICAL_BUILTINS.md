# Boolean reductions

The ordinary typed builtin registry owns `all` and `any`. Each takes exactly one
bool scalar or vec2/vec3/vec4<bool> and returns scalar bool. Scalar input is an
identity; vector input reduces every component by conjunction or disjunction.
Numeric and aggregate operands reject without implicit conversion. Name lookup
continues to admit ordinary user functions and aliases with these spellings.
The [WGSL logical builtin contract](https://www.w3.org/TR/WGSL/#logical-builtin-functions)
is the semantic authority.

Argument evaluation uses the existing ordered call owner. The complete operand,
including every effectful vector constructor component, evaluates once before
reduction. Constant evaluation projects the existing scalar/zero/repeat/composite
DAG. Override and runtime stages retain their independent flags; an unknown
required override is never replaced with false. ABI13 controls exercise the
result as a deferred workgroup-size dependency and check both supplied values.

The existing unary IR_BUILTIN record carries the saved operand. Scalar lowering
copies that value; vector lowering uses SPIR-V OpAll/OpAny and explicit HLSL/MSL
all/any intrinsics. These are per-invocation logical operations. Targets validate
boolean input/scalar output and keep their closed intrinsic mapping. No public
ABI, workspace, parser, pool, traversal or recursive-depth limit changes.

`make all-any-check` exercises all three public target adapters, exhaustive
constant vector masks through independently expected reflected array sizes,
scalar identities, wrong kinds/arity, name shadowing, eager snapshots, failed
publication and caller FP/errno state. `make test-specialization` includes the
all/any override-stage dependency controls; `make sanitize-all-any` instruments
the full compiler source. SDK validation and public execution retain separate
source-bound evidence.

[all_any_retained.wgsl](tests/all_any_retained.wgsl) preserves the literal source
from the retained [Zig builtin test](../../zig/tests/wgsl/emit_spirv_builtin_test.zig).
Dispatching one invocation writes the literal value 3 to its one-word binding.
[all_any_cases.wgsl](tests/all_any_cases.wgsl) declares a 32-word input/output
contract and a literal result table for two seed flags. It covers vector widths,
an OOB read at the binding edge and a maximum index (clamped to the last word,
seed 99, by HLSL and SPIR-V; zero under the MSL guard), traces 1234/5678 proving
all constructor effects occur, and once-only scalar/vector-producing helper
calls. Allocation prefix/suffix guards belong to its eventual public consumer;
compiler/SDK checks alone do not establish GPU pixel/data results or qualification.
