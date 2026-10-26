/* SPDX-License-Identifier: MIT
 * Text-target ABI names are bounded; source identity remains the complete span. */
#ifndef DOE_TARGET_ENTRY_NAME_H
#define DOE_TARGET_ENTRY_NAME_H
#include "../src/internal.h"
#include <string.h>

static inline bool target_entry_ascii(const Source *source, NameSpan name) {
    if (!name.length || name.offset > source->length || name.length > source->length - name.offset) return false;
    for (uint32_t i = 0u; i < name.length; ++i) {
        const char character = source->source[name.offset + i];
        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (i != 0u && character >= '0' && character <= '9') || character == '_')) return false;
    }
    return true;
}
static inline bool target_entry_short_name(const Source *source, NameSpan name) {
    static const char prefix[] = "doe_long_entry_";
    if (name.offset > source->length || name.length > source->length - name.offset) return false;
    return name.length > 63u ||
        (name.length >= sizeof(prefix) - 1u &&
         memcmp(source->source + name.offset, prefix, sizeof(prefix) - 1u) == 0);
}
#endif
