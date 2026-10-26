// SPDX-License-Identifier: MIT
// Text-buffer helpers over the shared growable CslBuf (csl_text.h).
// Divergence from csl_text_buffer.zig, by design of this port's shared
// infrastructure: the Zig buffer is fixed-size and fails closed without
// advancing on overflow; the shared CslBuf grows (up to 1 GiB) and only
// then returns CSL_ERR_TOO_LARGE. JSON escaping here also escapes every
// control byte as \u00XX, where Zig writes non \n\r\t control bytes raw.
// Both differences change bytes only in those edge cases.
#ifndef CSL_TEXT_BUFFER_H
#define CSL_TEXT_BUFFER_H
#include <stdint.h>
#include "csl_text.h"
CslErr csl_text_write(CslBuf *buf, const char *text);
CslErr csl_text_write_int(CslBuf *buf, int64_t value);
CslErr csl_text_write_u64(CslBuf *buf, uint64_t value);
/* Zig writeJsonString: quoted string with escapes. */
CslErr csl_text_write_json_string(CslBuf *buf, const char *value);
#endif
