// SPDX-License-Identifier: MIT
// Section headers and extraction mirroring csl_compile_sections.zig.
// A combined CSL output is: separator + filename + separator-end +
// body, repeated for layout.csl then pe_program.csl.
#ifndef CSL_COMPILE_SECTIONS_H
#define CSL_COMPILE_SECTIONS_H

#include <stddef.h>
#include "csl_text.h"

CslErr csl_section_write_header(CslBuf *buf, const char *filename);
/* Returns a pointer into `csl` (length via out_len) for the named
 * section body, or NULL when the section is absent. */
const char *csl_section_body(const char *csl, const char *filename,
    size_t *out_len);

#endif
