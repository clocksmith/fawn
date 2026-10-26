/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_schema.h"

#include <stdlib.h>
#include <string.h>

size_t tsir_text_length(const char *text, size_t length) {
    return length != 0 ? length : (text == NULL ? 0 : strlen(text));
}

bool tsir_text_equal(const char *left, size_t left_length,
    const char *right, size_t right_length) {
    size_t a = tsir_text_length(left, left_length);
    size_t b = tsir_text_length(right, right_length);
    return left != NULL && right != NULL && a == b &&
        (a == 0 || memcmp(left, right, a) == 0);
}

bool tsir_text_starts_with(const char *text, size_t length, const char *prefix) {
    size_t count = tsir_text_length(text, length);
    size_t prefix_count = tsir_text_length(prefix, 0);
    return text != NULL && prefix != NULL && count >= prefix_count &&
        (prefix_count == 0 || memcmp(text, prefix, prefix_count) == 0);
}

char *tsir_text_duplicate(const char *text, size_t length) {
    size_t count = tsir_text_length(text, length);
    char *copy = NULL;
    if (text == NULL || count == SIZE_MAX) {
        return NULL;
    }
    copy = malloc(count + 1);
    if (copy != NULL) {
        memcpy(copy, text, count);
        copy[count] = '\0';
    }
    return copy;
}
