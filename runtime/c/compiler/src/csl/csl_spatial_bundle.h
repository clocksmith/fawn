// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_bundle_emitter.zig (UNTESTED).
// Status: partial. Bundle splitting and option lookup are ported. The
// WGSL to CSL translation step is NOT ported and fails visibly.
// No file or directory is created by this module; the caller writes the
// split outputs. Parity is NOT verified. Do not push as a parity claim.
#ifndef DOE_CSL_SPATIAL_BUNDLE_H
#define DOE_CSL_SPATIAL_BUNDLE_H

#include <stddef.h>

#include "csl_error.h"
#include "csl_text.h"

/* Exact markers from the Zig source, including the trailing newline. */
#define CSL_SPATIAL_LAYOUT_MARKER "//--- layout.csl ---\n"
#define CSL_SPATIAL_PE_PROGRAM_MARKER "//--- pe_program.csl ---\n"

/* Borrowed slice into the caller's bundle buffer. No allocation. */
typedef struct CslSpatialSlice {
    const char *data;
    size_t len;
} CslSpatialSlice;

typedef struct CslSpatialBundleSplit {
    CslSpatialSlice layout;
    CslSpatialSlice pe_program;
} CslSpatialBundleSplit;

/*
 * Split a CSL bundle at the first layout marker and the first pe_program
 * marker. Mirrors the Zig rules: the pe_program marker must start after
 * the end of the layout marker, both trimmed sides (bytes '\n', '\r',
 * ' ') must be non-empty, otherwise CSL_ERR_INVALID.
 */
CslErr csl_spatial_bundle_split(const char *bundle, size_t len,
                                CslSpatialBundleSplit *out);

/*
 * Find the value following the first exact occurrence of `name` in
 * argv[1..argc-1]. Returns CSL_ERR_INVALID when the name is absent and
 * also when it is present without a following value; the Zig source
 * distinguishes those two cases (MissingRequiredOption versus
 * MissingOptionValue) but the shared CslErr enum has no such pair, so
 * both map to CSL_ERR_INVALID here. The returned pointer borrows argv.
 */
CslErr csl_spatial_bundle_option(const char *const *argv, int argc,
                                 const char *name, const char **out_value);

/* Usage text for the bundle emitter tool, written into `out`. */
CslErr csl_spatial_bundle_usage(CslBuf *out);

/*
 * Compose "<dir>/<file>" into `out`. The caller owns any directory
 * creation and file writing; this only builds the path string.
 */
CslErr csl_spatial_bundle_join_path(CslBuf *out, const char *dir,
                                    const char *file);

/*
 * NOT PORTED: WGSL to CSL translation (doe_wgsl.translateToCsl in Zig).
 * Always returns CSL_ERR_UNSUPPORTED. A caller that needs a real bundle
 * must supply one produced by the translator; this module never invents
 * bundle text.
 */
CslErr csl_spatial_bundle_translate_wgsl(const char *wgsl_source,
                                         size_t wgsl_len, CslBuf *out);

#endif
