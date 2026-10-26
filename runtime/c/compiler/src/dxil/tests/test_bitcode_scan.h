/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_TEST_BITCODE_SCAN_H
#define DOE_DXIL_TEST_BITCODE_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "dxil_spec.h"

/* Test-side LLVM bitcode scanner shared by the DXIL tests: walks the
 * writer's own stream format (unabbreviated records only, which is
 * all the writer emits) and counts FUNCTION-block CALL/RET records
 * plus METADATA string payloads. Not shipped code. */
typedef struct DxilScanReader {
    const uint8_t *buf;
    size_t len;
    size_t byte_pos;
    unsigned bit_pos;
} DxilScanReader;

static uint64_t scan_read_bits(DxilScanReader *reader, unsigned count) {
    uint64_t value = 0;
    unsigned shift = 0;
    unsigned remaining = count;
    while (remaining > 0) {
        unsigned avail = 8u - reader->bit_pos;
        unsigned take = remaining < avail ? remaining : avail;
        unsigned mask = (unsigned)((1u << take) - 1u);
        unsigned chunk = 0;
        if (reader->byte_pos >= reader->len) {
            return value;
        }
        chunk = (unsigned)((reader->buf[reader->byte_pos]
            >> reader->bit_pos) & mask);
        value |= (uint64_t)chunk << shift;
        shift += take;
        remaining -= take;
        reader->bit_pos += take;
        if (reader->bit_pos == 8) {
            reader->bit_pos = 0;
            reader->byte_pos += 1;
        }
    }
    return value;
}

static uint64_t scan_read_vbr(DxilScanReader *reader, unsigned width) {
    uint64_t value = 0;
    unsigned shift = 0;
    for (;;) {
        uint64_t chunk = scan_read_bits(reader, width);
        uint64_t data_mask = (width >= 64) ? UINT64_MAX
            : ((UINT64_C(1) << (width - 1)) - 1u);
        uint64_t continue_bit = UINT64_C(1) << (width - 1);
        value |= (chunk & data_mask) << shift;
        shift += width - 1;
        if ((chunk & continue_bit) == 0) {
            break;
        }
    }
    return value;
}

static void scan_align32(DxilScanReader *reader) {
    size_t remainder = 0;
    if (reader->bit_pos != 0) {
        reader->bit_pos = 0;
        reader->byte_pos += 1;
    }
    remainder = reader->byte_pos % 4;
    if (remainder != 0) {
        reader->byte_pos += 4 - remainder;
    }
}

typedef struct DxilScanResult {
    bool ok;
    unsigned function_blocks;
    unsigned call_records;
    unsigned last_function_code;
    bool function_saw_ret_last;
    char metadata_text[8192];
    size_t metadata_len;
} DxilScanResult;

static bool scan_bitcode(const uint8_t *bitcode, size_t bitcode_len,
    DxilScanResult *out) {
    DxilScanReader reader;
    unsigned width = 2;
    unsigned block_stack[16];
    unsigned width_stack[16];
    unsigned depth = 0;
    unsigned function_call_count = 0;
    unsigned function_last_code = 0;
    bool function_has_records = false;
    memset(out, 0, sizeof(*out));
    if (bitcode_len < 4 || bitcode[0] != 'B' || bitcode[1] != 'C'
        || bitcode[2] != 0xC0 || bitcode[3] != 0xDE) {
        return false;
    }
    reader.buf = bitcode;
    reader.len = bitcode_len;
    reader.byte_pos = 4;
    reader.bit_pos = 0;
    for (;;) {
        unsigned abbrev = 0;
        if (reader.byte_pos >= reader.len && depth == 0) {
            break;
        }
        abbrev = (unsigned)scan_read_bits(&reader, width);
        if (abbrev == DXIL_ABBREV_END_BLOCK) {
            scan_align32(&reader);
            if (depth == 0) {
                break;
            }
            depth -= 1;
            width = width_stack[depth];
            if (depth == 0) {
                break;
            }
            continue;
        }
        if (abbrev == DXIL_ABBREV_ENTER_SUBBLOCK) {
            uint64_t block_id = scan_read_vbr(&reader, 8);
            uint64_t new_width = scan_read_vbr(&reader, 4);
            scan_align32(&reader);
            reader.byte_pos += 4;
            if (depth >= 16) {
                return false;
            }
            block_stack[depth] = (unsigned)block_id;
            width_stack[depth] = width;
            depth += 1;
            width = (unsigned)new_width;
            if (block_id == DXIL_BLOCK_FUNCTION) {
                out->function_blocks += 1;
                function_call_count = 0;
                function_last_code = 0;
                function_has_records = false;
            }
            continue;
        }
        if (abbrev == DXIL_ABBREV_UNABBREV_RECORD) {
            uint64_t code = scan_read_vbr(&reader, 6);
            uint64_t num_ops = scan_read_vbr(&reader, 6);
            uint64_t i = 0;
            unsigned top = depth > 0 ? block_stack[depth - 1] : 0;
            if (top == DXIL_BLOCK_FUNCTION) {
                if (code == DXIL_FUNCTION_INST_CALL) {
                    function_call_count += 1;
                }
                function_last_code = (unsigned)code;
                function_has_records = true;
            }
            if (top == DXIL_BLOCK_METADATA
                && (code == DXIL_METADATA_STRING
                    || code == DXIL_METADATA_NAME)) {
                for (i = 0; i < num_ops; i++) {
                    uint64_t byte = scan_read_vbr(&reader, 6);
                    if (out->metadata_len + 1
                        < sizeof(out->metadata_text)) {
                        out->metadata_text[out->metadata_len] =
                            (char)byte;
                        out->metadata_len += 1;
                        out->metadata_text[out->metadata_len] =
                            '\0';
                    }
                }
                continue;
            }
            for (i = 0; i < num_ops; i++) {
                (void)scan_read_vbr(&reader, 6);
            }
            if (top == DXIL_BLOCK_FUNCTION) {
                out->call_records = function_call_count;
                out->last_function_code = function_last_code;
                out->function_saw_ret_last =
                    function_has_records
                    && function_last_code == DXIL_FUNCTION_INST_RET;
            }
            continue;
        }
        return false;
    }
    out->ok = true;
    return true;
}

/* Extracts the DXIL program part's bitcode from a DXBC container. */
static bool container_bitcode(const uint8_t *container,
    size_t container_len, const uint8_t **out_bitcode,
    size_t *out_len) {
    uint32_t part_count = 0;
    uint32_t i = 0;
    if (container_len < 36 || memcmp(container, "DXBC", 4) != 0) {
        return false;
    }
    part_count = dxil_test_read_u32le(container, 28);
    for (i = 0; i < part_count; i++) {
        uint32_t offset = dxil_test_read_u32le(container, 32 + 4 * i);
        uint32_t part_size = 0;
        uint32_t bitcode_offset = 0;
        uint32_t bitcode_size = 0;
        if (offset + 8 > container_len) {
            return false;
        }
        if (memcmp(container + offset, "DXIL", 4) != 0) {
            continue;
        }
        part_size = dxil_test_read_u32le(container, offset + 4);
        if (offset + 8 + part_size > container_len) {
            return false;
        }
        bitcode_offset =
            dxil_test_read_u32le(container, offset + 8 + 8);
        bitcode_size =
            dxil_test_read_u32le(container, offset + 8 + 12);
        *out_bitcode = container + offset + 8 + bitcode_offset;
        *out_len = bitcode_size;
        return true;
    }
    return false;
}

#endif /* DOE_DXIL_TEST_BITCODE_SCAN_H */
