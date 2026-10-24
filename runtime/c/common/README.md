# Shared backend leaves

`include/doe_copy_footprint.h` is a dependency-free C11 arithmetic leaf shared by
resource copy admission and native transfer lowering. It counts bytes through the
final occupied row, not trailing row/image padding. Callers retain format, zero
extent, pitch normalization, alignment, offsets and capacity policies.

Developer check: `make -C runtime/c/common check`. The bounded independent oracle
covers 59,049 combinations; explicit overflow cases verify untouched outputs and
boundary/zero behavior. Authored/developer-checked, not a GPU qualification.

`include/doe_dispatch_guard.h` constructs the exact compute guard WGSL shared by
Vulkan and D3D12. The compiled guard zeroes all three private GPU scratch counts
when any count exceeds its admitted limit. Backend callers own scratch copies,
ordering, native reflection and resource leases; the compiler owns WGSL meaning.
The source builder publishes neither text nor length on failure.

`tests/test_dispatch_guard.c` checks construction and output preservation.
`../native/tools/check_d3d12_indirect.py` additionally compiles the real source
through the public C HLSL facade and checks typed binding/entry metadata. Those
are CPU source/metadata controls, not SDK or GPU execution evidence.
