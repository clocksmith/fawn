// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_pe_program_metadata.zig (UNTESTED).
// Status: done. The parser recognizes exactly the declaration shapes the
// Zig module documents (typed var, @zeros var, pointer var,
// @export_symbol, plus param/const lines with defaults, plus layout
// @export_name lines). It is not a general CSL parser. All parsed
// strings borrow the caller's source buffer; the caller must keep the
// source alive while using a parsed result. Parity is NOT verified.
#ifndef DOE_CSL_SPATIAL_PE_METADATA_H
#define DOE_CSL_SPATIAL_PE_METADATA_H

#include <stddef.h>

#include "csl_error.h"
#include "csl_text.h"

/* Caps from the Zig source. Entries beyond a cap are silently dropped, */
/* exactly as in Zig; see spatial-mapping.md for the note on this rule. */
#define CSL_SPATIAL_PE_MAX_DECLS 64
#define CSL_SPATIAL_PE_MAX_POINTERS 64
#define CSL_SPATIAL_PE_MAX_EXPORTS 64
#define CSL_SPATIAL_PE_MAX_CONSTANTS 64
#define CSL_SPATIAL_PE_MAX_LAYOUT_EXPORTS 64

/* Borrowed string view into the caller's source text. */
typedef struct CslSpatialStr {
    const char *data;
    size_t len;
} CslSpatialStr;

typedef struct CslSpatialPeDecl {
    CslSpatialStr name;
    CslSpatialStr size_expr;
    CslSpatialStr elem_type;
} CslSpatialPeDecl;

typedef struct CslSpatialPePointer {
    CslSpatialStr name;
    CslSpatialStr backing;
    CslSpatialStr elem_type;
} CslSpatialPePointer;

typedef struct CslSpatialPeExport {
    CslSpatialStr symbol;
    CslSpatialStr pointer;
} CslSpatialPeExport;

typedef struct CslSpatialPeConstant {
    CslSpatialStr kind; /* "const" or "param" */
    CslSpatialStr name;
    CslSpatialStr type_name;
    CslSpatialStr expr;
} CslSpatialPeConstant;

typedef struct CslSpatialPeParsed {
    CslSpatialPeDecl decls[CSL_SPATIAL_PE_MAX_DECLS];
    size_t decl_count;
    CslSpatialPePointer pointers[CSL_SPATIAL_PE_MAX_POINTERS];
    size_t pointer_count;
    CslSpatialPeExport exports[CSL_SPATIAL_PE_MAX_EXPORTS];
    size_t export_count;
    CslSpatialPeConstant constants[CSL_SPATIAL_PE_MAX_CONSTANTS];
    size_t constant_count;
} CslSpatialPeParsed;

typedef struct CslSpatialLayoutExport {
    CslSpatialStr name;
    CslSpatialStr type_str;
    CslSpatialStr kind; /* "device_function" or "device_variable" */
    int mutable_flag;
} CslSpatialLayoutExport;

typedef struct CslSpatialLayoutParsed {
    CslSpatialLayoutExport exports[CSL_SPATIAL_PE_MAX_LAYOUT_EXPORTS];
    size_t export_count;
} CslSpatialLayoutParsed;

/* Per-compile-target descriptor for targets.metadata.json. phase may */
/* be NULL, which emits JSON null, exactly as in Zig. All strings are */
/* borrowed NUL-terminated caller strings. */
typedef struct CslSpatialTargetDescriptor {
    const char *name;
    const char *base_kernel;
    const char *phase;
    const char *layout;
    const char *pe_program;
} CslSpatialTargetDescriptor;

CslErr csl_spatial_pe_parse(const char *source, size_t len,
                            CslSpatialPeParsed *out);
CslErr csl_spatial_pe_parse_layout(const char *source, size_t len,
                                   CslSpatialLayoutParsed *out);

/* Emit pe_program.metadata.json text into `out` (caller writes it). */
CslErr csl_spatial_pe_emit_json(const char *source, size_t len, CslBuf *out);
/* Emit layout.metadata.json text into `out` (caller writes it). */
CslErr csl_spatial_pe_emit_layout_json(const char *source, size_t len,
                                       CslBuf *out);
/* Emit compile/targets.metadata.json text into `out`. */
CslErr csl_spatial_pe_emit_targets_json(
    const CslSpatialTargetDescriptor *targets, size_t count, CslBuf *out);

#endif
