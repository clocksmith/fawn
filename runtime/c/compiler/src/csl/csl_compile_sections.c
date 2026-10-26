// SPDX-License-Identifier: MIT
// Section headers and extraction mirroring csl_compile_sections.zig.
#include "csl_compile_sections.h"
#include "csl_text.h"

#include <string.h>
#include "csl_spec.h"

CslErr csl_section_write_header(CslBuf *buf, const char *filename) {
    CslErr err = csl_buf_puts(buf, CSL_SECTION_SEPARATOR);
    if (err != CSL_OK) {
        return err;
    }
    err = csl_buf_puts(buf, filename);
    if (err != CSL_OK) {
        return err;
    }
    return csl_buf_puts(buf, CSL_SECTION_SEPARATOR_END);
}

const char *csl_section_body(const char *csl, const char *filename,
    size_t *out_len) {
    char marker[256];
    size_t marker_len;
    const char *header;
    const char *body_start;
    const char *next;
    if (csl == NULL || filename == NULL) {
        return NULL;
    }
    marker_len = strlen(CSL_SECTION_SEPARATOR) + strlen(filename) +
        strlen(CSL_SECTION_SEPARATOR_END);
    if (marker_len >= sizeof(marker)) {
        return NULL;
    }
    marker[0] = '\0';
    strcat(marker, CSL_SECTION_SEPARATOR);
    strcat(marker, filename);
    strcat(marker, CSL_SECTION_SEPARATOR_END);
    header = strstr(csl, marker);
    if (header == NULL) {
        return NULL;
    }
    body_start = header + marker_len;
    next = strstr(body_start, CSL_SECTION_SEPARATOR);
    if (out_len != NULL) {
        *out_len = next == NULL ? strlen(body_start)
            : (size_t)(next - body_start);
    }
    return body_start;
}
