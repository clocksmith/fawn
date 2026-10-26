/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_VALIDATE_H
#define DOE_DXIL_VALIDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* DXBC container structural validation. Validates Microsoft's actual
 * program-header layout, correcting the inherited Zig offset mismatch.
 * Checks the binary envelope (DXBC header magic,
 * version, part table bounds, DXIL program sub-header, LLVM bitcode
 * magic) without checking shader semantics.
 *
 * This function never fails by return code: it always fills the result
 * struct, and callers inspect `valid` and `error_message`. On any
 * failure the result reports no DXIL part, no HASH part, and no
 * optional fields, even if a DXIL part was already seen, exactly as
 * Zig's errResult does. */

typedef enum DxilValidationError {
    DXIL_VALIDATION_NONE = 0,
    DXIL_VALIDATION_TOO_SMALL,
    DXIL_VALIDATION_BAD_MAGIC,
    DXIL_VALIDATION_BAD_VERSION,
    DXIL_VALIDATION_SIZE_MISMATCH,
    DXIL_VALIDATION_PART_OFFSET_OUT_OF_BOUNDS,
    DXIL_VALIDATION_PART_COUNT_TOO_LARGE,
    DXIL_VALIDATION_NO_DXIL_PART,
    DXIL_VALIDATION_DXIL_PART_TOO_SMALL,
    DXIL_VALIDATION_BAD_BITCODE_MAGIC
} DxilValidationError;

typedef struct DxilValidationResult {
    bool valid;
    uint32_t container_size;
    uint32_t part_count;
    bool has_dxil_part;
    bool has_hash_part;
    /* Present only on success, when a DXIL part was found. Note: these
     * are the SHADER MODEL fields from program-header word 0, even
     * though Zig names them dxil_major/dxil_minor. */
    bool has_shader_kind;
    uint32_t shader_kind;
    bool has_dxil_major;
    uint16_t dxil_major;
    bool has_dxil_minor;
    uint16_t dxil_minor;
    bool has_bitcode_size;
    uint32_t bitcode_size;
    /* NULL on success; otherwise a static message string (never freed). */
    const char *error_message;
    DxilValidationError error;
} DxilValidationResult;

void dxil_validate(const uint8_t *data, size_t len,
    DxilValidationResult *result);

#endif /* DOE_DXIL_VALIDATE_H */
