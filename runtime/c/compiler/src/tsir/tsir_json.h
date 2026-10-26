/* SPDX-License-Identifier: MIT
 * Reused verbatim from runtime/c/runtime-cli/src/json.h at fawn-tree
 * head 43040741d (bounded JSON reader, not hand-rolled here).
 * Bounded JSON reader for runtime command streams (RFC 8259). One document owns
 * every value and decoded string; nothing borrows the input after parsing.
 * Integers without fraction or exponent keep their exact 64-bit magnitude. */
#ifndef DOE_TSIR_JSON_H
#define DOE_TSIR_JSON_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { DOE_JSON_MAX_DEPTH = 64 };

typedef enum doe_json_kind {
    DOE_JSON_NULL,
    DOE_JSON_BOOL,
    DOE_JSON_NUMBER,
    DOE_JSON_STRING,
    DOE_JSON_ARRAY,
    DOE_JSON_OBJECT
} doe_json_kind;

typedef enum doe_json_status {
    DOE_JSON_OK,
    DOE_JSON_SYNTAX,
    DOE_JSON_DEPTH,
    DOE_JSON_DUPLICATE_KEY,
    DOE_JSON_OUT_OF_MEMORY
} doe_json_status;

typedef struct doe_json_value doe_json_value;
typedef struct doe_json_member {
    const char *key; /* NUL terminated, owned by the document. */
    size_t key_size;
    doe_json_value *value;
} doe_json_member;

struct doe_json_value {
    doe_json_kind kind;
    bool boolean;
    double number;
    /* Exact integer form: set only for numbers without fraction or exponent
     * whose magnitude fits in 64 bits. */
    bool integer;
    bool negative;
    uint64_t magnitude;
    const char *string; /* NUL terminated, may contain embedded NUL via \u0000. */
    size_t string_size;
    doe_json_value **items;
    doe_json_member *members;
    size_t count;
};

typedef struct doe_json_document {
    doe_json_value *root;
    void **allocations;
    size_t allocation_count, allocation_capacity;
} doe_json_document;

typedef struct doe_json_error {
    doe_json_status status;
    size_t offset; /* Byte offset of the failure in the input. */
} doe_json_error;

/* On failure *document is left empty (nothing to free). */
doe_json_status doe_json_parse(const char *text, size_t size, doe_json_document *document, doe_json_error *error);
void doe_json_free(doe_json_document *document);

const doe_json_value *doe_json_member_value(const doe_json_value *object, const char *key);
/* Exact unsigned integer within [0, max]; false for any other value. */
bool doe_json_as_u64(const doe_json_value *value, uint64_t max, uint64_t *out);
const char *doe_json_status_name(doe_json_status status);
#endif
