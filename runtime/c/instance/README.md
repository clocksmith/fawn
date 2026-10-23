# Experimental C native-instance owner

The [frozen ownership contract](CONTRACT.md), [C header](include/doe_instance.h)
and [retained native seam](integration/CONTRACT.md) define the next incremental
owner after accepted C futures and callbacks. Implementation and independent
acceptance are not yet claimed. [Author requirements](EVALUATION.md) do not
substitute for independently sealed cases or observed results.

The boundary moves the real native instance allocation, sole atomic reference
count and queue lifetime into C. Its copied queue/lease view is explicitly
non-owning. Actual retained native instance, callback and unconfigured surface
consumers remain required; no simulated device or backend can close these gates.
The new selection stays default-off and requires both accepted C owners.

See [provenance](PROVENANCE.md), [source identity](SOURCE_INPUTS.json),
[implementation style](STYLE.md) and the
[future-adapter amendment](../futures/integration/INSTANCE_VIEW_AMENDMENT.md).
Build/reproduction commands will accompany the actual implementation, not a
placeholder executable. No system installation, public publication or production
switch is part of this contract.
