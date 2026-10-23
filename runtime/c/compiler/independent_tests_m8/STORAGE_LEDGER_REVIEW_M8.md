# Independent M8 storage / IR / target / stack review

Bound to candidate1 manifest a877ba32e0328f0e98d9c6d62f982dc706b657d7a73c1280a5034b0b9961ebba.
Reviewed actual storage.c, validate.c and spirv.c after exact preinspection seal,
not developer test expectations. Author ledger was consulted as a claim to check.

## Checked caller storage

reserve_array checks alignment padding before advancing and count*element against
remaining SIZE_MAX. compiler_job_layout bounds source/token/E/S before additions,
computes I=1+4E+8S and256+32I in uint64, rejects final header-bound overflow, and
then separately lays out statements, physical expressions, C records, token+end,
instructions, locals, scope, values and staging words. Query outputs are assigned
only after the entire layout succeeds; bind uses the same validated limits.
C=E+1 includes one old exact exempt constant, while each new declaration has a
charged initializer node. Fixed physical expression storage separately preserves
old if-wrapper/leading-constant exemptions. Actual capacities remain binding even
with extra supplied workspace; focused tests repeat this three times.

Dependency resolution validate.c905-950 uses a loop and each unresolved constant's
scan/parent fields. A parent is saved before replacing that union with final bits.
Every initializer name node is scanned, including skipped logical RHS; cycle
edges are detected in visiting state before numeric publication. References use
resolved NumericValue, not recursive declaration evaluation. Exact chains up to
1024 declarations and258-node cycles were exercised. Source capacity does not
increase recursion depth or create host-sized temporary arrays.

## Whole IR bound

append checks385 fixed operations or configuredI before every write. expression_emit
suppresses appends when emit=false; ensure_value also returns without appending in
that state. mandatory_rhs reevaluation, module constants and attributes explicitly
pass emit=false. Folding is value-only until one concrete consumer requires one
IR_CONSTANT; abstract intermediates never emit executable values. Localconst
initializers cannot contain runtime paths and therefore do not hide emitted IR.

Per original source node, excluding child charges: literal/folded/name concrete
consumer<=1; mutable/input/member/load<=1; unary/binary/compare/max/conversion/
bitcast<=1; logical short circuit<=2; parentheses0. Immutable let reads reuse IDs.
Materialization mutates an unemitted typed payload, not extra IR. Every additional
constant ensure belongs to an operand's charged source node. Thus4E is conservative.
Per statement, excluding expressions/children: local const/let/block/empty0;
var<=2; assignment1; compound<=3; increment/decrement<=4; if3 markers plus at most
one historical uncharged comparison; loop4 markers; control exit1. For initializer
and update are separately charged S. Thus8S plus one old leading alias is safe.
Dead source is still validated but each source tree is lowered at most once for
emission; the separate mandatory re-evaluation does not append.

Observed boundaries: original315-node/15-statement robust logical source passes
job interpretation and hits fixed4096-word capacity. Corrected additional balanced
mutable-bool source has319nodes/16statements and exceeds385 semantic operations;
it passes job execution and fails both fixed adapters with named typed-instruction
capacity and atomic outputs. This distinction is documented in evaluator errata.

## Whole target ID bound

Independently counted every reserve_ids site in spirv.c, including the ordinary
result reservation at emit_block520 and module constants at221:

- Constant/local load/unary/binary/compare/bitcast:1.
- Vector component input/uniform member load:2.
- Max/masked shifts:2.
- Unsigned robust load/store:7/5; signed robust load/store:10/8.
- Unsigned div/rem:4; signed div/rem:8; float div/rem:1.
- f32-to-integer conversion:7; other conversion:1.
- If3, short-begin2, short-end1, loop-begin5; other markers0.
- Each mutable Function variable ID is charged to its required declaration store.

All are <=32. Fixed global starting IDs below31 plus buffers8, inputs5, uniform
pointers2, struct pairs16, member-index constants8, local pointer types4 and entry
label1 total at most75, below256. Module constants require no extra global IDs
unless a consumer emits an IR_CONSTANT, already charged above. Source-unused
constants remain value records. reserve_ids checks each actual reservation;
serialization independently checks word capacity. The job layout includes the
final SPIR-V header bound, not only result IDs. All target/value indexes stay
within I+1 caller-owned slots. Exact overflowing E/S and SIZE_MAX queries reject
without touching requirements output.

## Native size and stack closure

Independent GCC14.2 flags are -O2 -fno-fast-math -ffp-contract=off -frounding-math.
Measured LegacyWorkspace76512<=77760, ABI1 frame76592<=77824, all other library
frames<=1344<=8192. No VLA/unbounded dynamic frame. Native audit and callgraph
receipts are in native_host/candidate1-native, with all headers/exports unchanged.

Retained job64/32 and fixed18/4 expression/control envelopes, compound2C+1 and
emitterD+C limits govern the same recursive SCCs. There is no dependency-count
recursive SCC. To avoid understating larger M8 frames, use these deliberately
broad conservative sums, excluding adapters/callers, not measured high-water marks:
parser(2C+2)*144+(D+1)*384+1024=35488 bytes;
semantic(2C+2)*624+(D+1)*(416+144+112)+4096=88960 bytes;
emitter(D+C+1)*208+2048=22224 bytes at job maxima. The semantic sum intentionally
charges both optional wrappers at every expression depth and includes the1344-byte
numeric leaf in4096. These new sums exceed old M7 coarse estimates; no reduced-total-
stack claim is made. Frozen contracts impose explicit frame/workspace ceilings and
retained nesting/count bounds, not an immutable aggregate-byte estimate.

Verdict: ledger, explicit storage/IR/ID bounds and retained recursion envelope close
for candidate1. No waiver of word bounds, source capacity, host restoration, or
physical-backend qualification is implied.
