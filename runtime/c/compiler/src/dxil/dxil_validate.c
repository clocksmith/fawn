/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_validate.h"

#include <string.h>

#include "dxil_spec.h"

#define DXIL_VALIDATE_MAX_PART_COUNT 256u
#define DXIL_VALIDATE_PART_HEADER_SIZE 8u

static uint32_t read_u32(const uint8_t *data, size_t offset) {
    return (uint32_t)data[offset]
        | ((uint32_t)data[offset + 1] << 8)
        | ((uint32_t)data[offset + 2] << 16)
        | ((uint32_t)data[offset + 3] << 24);
}

static const char *error_message_for(DxilValidationError error) {
    switch (error) {
        case DXIL_VALIDATION_NONE: return NULL;
        case DXIL_VALIDATION_TOO_SMALL:
            return "container too small for DXBC header";
        case DXIL_VALIDATION_BAD_MAGIC:
            return "invalid DXBC or DXIL magic bytes";
        case DXIL_VALIDATION_BAD_VERSION:
            return "unsupported DXBC version (expected 1)";
        case DXIL_VALIDATION_SIZE_MISMATCH:
            return "container size field does not match data length";
        case DXIL_VALIDATION_PART_OFFSET_OUT_OF_BOUNDS:
            return "part offset exceeds container bounds";
        case DXIL_VALIDATION_PART_COUNT_TOO_LARGE:
            return "part count exceeds maximum (256)";
        case DXIL_VALIDATION_NO_DXIL_PART:
            return "no DXIL part found in container";
        case DXIL_VALIDATION_DXIL_PART_TOO_SMALL:
            return "DXIL part too small for program header or bitcode";
        case DXIL_VALIDATION_BAD_BITCODE_MAGIC:
            return "LLVM bitcode magic (0x4243C0DE) not found";
    }
    return "unknown validation failure";
}

static void fill_error_result(const uint8_t *data, size_t len,
    DxilValidationError error, DxilValidationResult *result) {
    memset(result, 0, sizeof(*result));
    result->valid = false;
    result->container_size = len >= 28 ? read_u32(data, 24) : (uint32_t)len;
    result->part_count = len >= 32 ? read_u32(data, 28) : 0;
    result->error_message = error_message_for(error);
    result->error = error;
}

void dxil_validate(const uint8_t *data, size_t len,
    DxilValidationResult *result) {
    uint32_t version = 0;
    uint32_t container_size = 0;
    uint32_t part_count = 0;
    uint64_t offset_table_end = 0;
    bool has_dxil_part = false;
    bool has_hash_part = false;
    bool has_shader_kind = false;
    uint32_t shader_kind = 0;
    bool has_dxil_major = false;
    uint16_t dxil_major = 0;
    bool has_dxil_minor = false;
    uint16_t dxil_minor = 0;
    bool has_bitcode_size = false;
    uint32_t bitcode_size = 0;
    uint32_t i = 0;
    uint8_t dxbc_magic[4];
    uint8_t dxil_fourcc[4];
    uint8_t hash_fourcc[4];
    uint8_t bitcode_magic[4];

    dxil_dxbc_fourcc_bytes(dxbc_magic);
    dxil_part_fourcc_bytes(DXIL_PART_FOURCC_DXIL, dxil_fourcc);
    dxil_part_fourcc_bytes(DXIL_PART_FOURCC_HASH, hash_fourcc);
    dxil_llvm_magic_bytes(bitcode_magic);

    if (len < DXIL_DXBC_HEADER_SIZE) {
        fill_error_result(data, len, DXIL_VALIDATION_TOO_SMALL, result);
        return;
    }
    if (memcmp(data, dxbc_magic, 4) != 0) {
        fill_error_result(data, len, DXIL_VALIDATION_BAD_MAGIC, result);
        return;
    }
    version = read_u32(data, 20);
    if (version != 1) {
        fill_error_result(data, len, DXIL_VALIDATION_BAD_VERSION, result);
        return;
    }
    container_size = read_u32(data, 24);
    if (container_size != (uint32_t)len) {
        fill_error_result(data, len, DXIL_VALIDATION_SIZE_MISMATCH, result);
        return;
    }
    part_count = read_u32(data, 28);
    if (part_count >= DXIL_VALIDATE_MAX_PART_COUNT) {
        fill_error_result(data, len, DXIL_VALIDATION_PART_COUNT_TOO_LARGE,
            result);
        return;
    }
    offset_table_end = (uint64_t)DXIL_DXBC_HEADER_SIZE
        + (uint64_t)part_count * 4u;
    if (offset_table_end > len) {
        fill_error_result(data, len,
            DXIL_VALIDATION_PART_OFFSET_OUT_OF_BOUNDS, result);
        return;
    }
    for (i = 0; i < part_count; i++) {
        const size_t offset_pos = DXIL_DXBC_HEADER_SIZE + (size_t)i * 4;
        const uint32_t part_offset = read_u32(data, offset_pos);
        const uint8_t *fourcc = NULL;
        uint32_t part_data_size = 0;
        uint64_t part_data_start = 0;
        if ((uint64_t)part_offset + DXIL_VALIDATE_PART_HEADER_SIZE > len) {
            fill_error_result(data, len,
                DXIL_VALIDATION_PART_OFFSET_OUT_OF_BOUNDS, result);
            return;
        }
        fourcc = data + part_offset;
        part_data_size = read_u32(data, part_offset + 4);
        part_data_start = (uint64_t)part_offset
            + DXIL_VALIDATE_PART_HEADER_SIZE;
        if (part_data_start + part_data_size > len) {
            fill_error_result(data, len,
                DXIL_VALIDATION_PART_OFFSET_OUT_OF_BOUNDS, result);
            return;
        }
        if (memcmp(fourcc, dxil_fourcc, 4) == 0) {
            const size_t prog_base = (size_t)part_data_start;
            uint32_t program_version = 0;
            uint64_t program_size = 0;
            uint32_t bc_offset = 0;
            uint32_t bc_size = 0;
            uint64_t bc_abs = 0;
            has_dxil_part = true;
            if (part_data_size < DXIL_PROGRAM_HEADER_SIZE) {
                fill_error_result(data, len,
                    DXIL_VALIDATION_DXIL_PART_TOO_SMALL, result);
                return;
            }
            /* DXC's bitcode offset is relative to the DXIL magic at
             * program+8, not to the program-version word. */
            if (memcmp(data + prog_base + 8, dxil_fourcc, 4) != 0) {
                fill_error_result(data, len, DXIL_VALIDATION_BAD_MAGIC, result);
                return;
            }
            program_size = (uint64_t)read_u32(data, prog_base + 4) * 4u;
            if (program_size < DXIL_PROGRAM_HEADER_SIZE
                || program_size > part_data_size) {
                fill_error_result(data, len,
                    DXIL_VALIDATION_DXIL_PART_TOO_SMALL, result);
                return;
            }
            program_version = read_u32(data, prog_base);
            shader_kind = program_version >> 16;
            dxil_major = (uint16_t)((program_version >> 4) & 0xFu);
            dxil_minor = (uint16_t)(program_version & 0xFu);
            has_shader_kind = true;
            has_dxil_major = true;
            has_dxil_minor = true;
            bc_offset = read_u32(data, prog_base + 16);
            bc_size = read_u32(data, prog_base + 20);
            bitcode_size = bc_size;
            has_bitcode_size = true;
            bc_abs = (uint64_t)prog_base + 8u + bc_offset;
            if (bc_offset < DXIL_PROGRAM_HEADER_SIZE - 8u || bc_size < 4
                || bc_abs + bc_size > part_data_start + program_size) {
                fill_error_result(data, len,
                    DXIL_VALIDATION_DXIL_PART_TOO_SMALL, result);
                return;
            }
            if (bc_size >= 4) {
                const size_t bc_start = (size_t)bc_abs;
                if (memcmp(data + bc_start, bitcode_magic, 4) != 0) {
                    fill_error_result(data, len,
                        DXIL_VALIDATION_BAD_BITCODE_MAGIC, result);
                    return;
                }
            }
        }
        if (memcmp(fourcc, hash_fourcc, 4) == 0) {
            has_hash_part = true;
        }
    }
    if (!has_dxil_part) {
        fill_error_result(data, len, DXIL_VALIDATION_NO_DXIL_PART, result);
        return;
    }
    memset(result, 0, sizeof(*result));
    result->valid = true;
    result->container_size = container_size;
    result->part_count = part_count;
    result->has_dxil_part = true;
    result->has_hash_part = has_hash_part;
    result->has_shader_kind = has_shader_kind;
    result->shader_kind = shader_kind;
    result->has_dxil_major = has_dxil_major;
    result->dxil_major = dxil_major;
    result->has_dxil_minor = has_dxil_minor;
    result->dxil_minor = dxil_minor;
    result->has_bitcode_size = has_bitcode_size;
    result->bitcode_size = bitcode_size;
    result->error_message = NULL;
    result->error = DXIL_VALIDATION_NONE;
}
