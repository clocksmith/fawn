# M7 amendment 2: shared empty-module grammar

Appended on 2026-10-23. This supersedes only amendment1's restriction of empty
module declarations to the job capability. Original contracts, seals, receipts
and the first failed witness remain unchanged.

Generic empty module declarations are admitted by the one shared parser through
ABI1, ABI2 and job API, and every CLI mode. There is no concrete caller dependency,
public layout difference or capacity reason requiring old APIs to reject this
valid WGSL syntax. Preserving an unpublished negative-test expectation alone is
not a reason to create a permanent language dialect.

Every semicolon still consumes source/token budget and no body-statement/scalar
node budget. Malformed interior semicolons and modules lacking the required entry
still reject. No other language, resource/reflection, fixed-capacity, native ABI,
job ownership or output-publication boundary changes in this amendment.

Record the old rejection and new independent expectation as a specifically
named intentional admission across modes. Do not edit historical frozen cases or
relax unrelated malformed/type-invalid cases. Fixed API resource limits and the
remaining M7 control-family capability boundary remain explicit for this tested
increment, not a permanent promise to reject valid WGSL in future versions.
