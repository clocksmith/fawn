# WGSL control resolver expectation repair

The first WGSL supplemental fixture aborts on the independently source-built
Z control because it incorrectly requires all WGSL functions to resolve through
GetProcAddress. P06 requires WGSL exports to remain equal to Z; the mandatory P01
proc set concerns the listed instance operations, not extra WGSL entries. The
repair still requires and executes all three direct WGSL exports, records the
three-bit actual proc availability for explicit/sentinel names, and executes
resolved sets when complete. Every selected provider must match Z availability
and values. Original failing source/receipt remain. Missing routes are reported
as unavailable, never counted as successful calls or relabelled candidate bugs.
