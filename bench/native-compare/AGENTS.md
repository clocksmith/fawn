# Native comparison recipes

Inherit [benchmark instructions](../AGENTS.md). Own native workload/config recipes
and their CLI contracts. Reusable implementation is in `../native_compare_modules/`;
GPU execution and lifetime stay in the runtime owner.

Preserve declared provider, cache, timing, output and cold/warm boundaries. Do not
promote single-provider telemetry or loss-only classification into a comparative win.
Run affected native-compare unit controls; new measurements require a named lane.
Registered evidence normalization belongs to `../inventory/`, not another parser here.
