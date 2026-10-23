# Checked copy footprint

`include/doe_copy_footprint.h` is a dependency-free C11 arithmetic leaf shared by
resource copy admission and native transfer lowering. It counts bytes through the
final occupied row, not trailing row/image padding. Callers retain format, zero
extent, pitch normalization, alignment, offsets and capacity policies.

Developer check: `make -C runtime/c/common check`. The bounded independent oracle
covers 59,049 combinations; explicit overflow cases verify untouched outputs and
boundary/zero behavior. Authored/developer-checked, not a GPU qualification.
