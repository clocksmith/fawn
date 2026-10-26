// SPDX-License-Identifier: MIT
// emit_csl_host_compile_rewrite.zig + emit_csl_rmsnorm_pack.zig +
// emit_csl_host_compile_source.zig (pattern table / sections part).
// Rewrite functions work on a caller-owned fixed buffer exactly like
// Zig (buf, *len, cap): first error wins, OutputTooLarge when a
// replacement would overflow, InvalidIr when a rmsnorm needle is absent.
// The embedded WGSL fixture sources and the WGSL-to-IR analysis step of
// compile_source are NOT ported (no fixture text in this port); pattern
// sections are produced from descriptors via csl_emit_core, then the
// f16 rewrite / fused-GEMV restore steps are applied in that order.
// Pattern sources: Zig patternSource() returns an EMBEDDED WGSL fixture
// string constant per pattern (ELEMENT_WISE_WGSL, etc.), not a filename.
// Those fixture texts are NOT embedded in this port; the table in
// csl_host_compile.c records only whether Zig has a fixture for a
// pattern, and sections come from descriptors via core.
#ifndef CSL_HOST_COMPILE_H
#define CSL_HOST_COMPILE_H
#include "csl_kernel.h"
CslErr csl_rewrite_f16_compile_source(char *buf, size_t *len, size_t cap);
CslErr csl_restore_fused_gemv_f32_reduction(char *buf, size_t *len, size_t cap);
CslErr csl_rewrite_f16_output_pack(char *buf, size_t *len, size_t cap);
CslErr csl_rewrite_f16_output_pack_for_extent(char *buf, size_t *len, size_t cap, const char *extent);
int csl_pattern_has_embedded_source(const char *pattern);
CslErr csl_emit_pattern_sections(CslBuf *out, const CslKernelDesc *d);
#endif
