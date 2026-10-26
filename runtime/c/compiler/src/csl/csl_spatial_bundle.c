// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_bundle_emitter.zig (UNTESTED).
// See the header for the ported versus unported split. Parity NOT verified.
#include "csl_spatial_bundle.h"

#include <string.h>

static int is_trim_byte(char c) {
    return c == '\n' || c == '\r' || c == ' ';
}

/* First occurrence of needle in hay[0..hay_len), or hay_len if absent. */
static size_t find_sub(const char *hay, size_t hay_len, const char *needle) {
    size_t nlen = strlen(needle);
    size_t i;
    if (nlen == 0 || hay_len < nlen) {
        return hay_len;
    }
    for (i = 0; i + nlen <= hay_len; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) {
            return i;
        }
    }
    return hay_len;
}

static CslSpatialSlice trim_slice(const char *data, size_t len) {
    size_t start = 0;
    size_t end = len;
    while (start < end && is_trim_byte(data[start])) {
        start++;
    }
    while (end > start && is_trim_byte(data[end - 1])) {
        end--;
    }
    {
        CslSpatialSlice out;
        out.data = data + start;
        out.len = end - start;
        return out;
    }
}

CslErr csl_spatial_bundle_split(const char *bundle, size_t len,
                                CslSpatialBundleSplit *out) {
    size_t layout_marker_len;
    size_t pe_marker_len;
    size_t layout_start;
    size_t pe_start;
    CslSpatialSlice layout;
    CslSpatialSlice pe_program;
    if (!bundle || !out) {
        return CSL_ERR_INVALID;
    }
    layout_marker_len = strlen(CSL_SPATIAL_LAYOUT_MARKER);
    pe_marker_len = strlen(CSL_SPATIAL_PE_PROGRAM_MARKER);
    layout_start = find_sub(bundle, len, CSL_SPATIAL_LAYOUT_MARKER);
    pe_start = find_sub(bundle, len, CSL_SPATIAL_PE_PROGRAM_MARKER);
    if (layout_start >= len || pe_start >= len) {
        return CSL_ERR_INVALID;
    }
    /* Zig: pe_start must be strictly after the end of the layout marker. */
    if (pe_start <= layout_start + layout_marker_len) {
        return CSL_ERR_INVALID;
    }
    layout = trim_slice(bundle + layout_start + layout_marker_len,
                        pe_start - (layout_start + layout_marker_len));
    pe_program = trim_slice(bundle + pe_start + pe_marker_len,
                            len - (pe_start + pe_marker_len));
    if (layout.len == 0 || pe_program.len == 0) {
        return CSL_ERR_INVALID;
    }
    out->layout = layout;
    out->pe_program = pe_program;
    return CSL_OK;
}

CslErr csl_spatial_bundle_option(const char *const *argv, int argc,
                                 const char *name, const char **out_value) {
    int i;
    if (!argv || argc < 0 || !name || !out_value) {
        return CSL_ERR_INVALID;
    }
    for (i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], name) == 0) {
            if (i + 1 >= argc) {
                return CSL_ERR_INVALID;
            }
            *out_value = argv[i + 1];
            return CSL_OK;
        }
    }
    return CSL_ERR_INVALID;
}

CslErr csl_spatial_bundle_usage(CslBuf *out) {
    if (!out) {
        return CSL_ERR_INVALID;
    }
    return csl_buf_puts(out, "usage: doe-csl-bundle-emitter --wgsl <source.wgsl> --out-dir <dir>\n");
}

CslErr csl_spatial_bundle_join_path(CslBuf *out, const char *dir,
                                    const char *file) {
    CslErr err;
    size_t dir_len;
    if (!out || !dir || !file) {
        return CSL_ERR_INVALID;
    }
    dir_len = strlen(dir);
    err = csl_buf_puts(out, dir);
    if (err) {
        return err;
    }
    if (dir_len > 0 && dir[dir_len - 1] != '/') {
        err = csl_buf_putc(out, '/');
        if (err) {
            return err;
        }
    }
    return csl_buf_puts(out, file);
}

CslErr csl_spatial_bundle_translate_wgsl(const char *wgsl_source,
                                         size_t wgsl_len, CslBuf *out) {
    (void)wgsl_source;
    (void)wgsl_len;
    (void)out;
    return CSL_ERR_UNSUPPORTED;
}
