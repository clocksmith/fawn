/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_container.h"

#include <string.h>

#include "dxil_md5.h"

static void write_u32(uint8_t *out, size_t *pos, uint32_t value) {
    out[*pos] = (uint8_t)value;
    out[*pos + 1] = (uint8_t)(value >> 8);
    out[*pos + 2] = (uint8_t)(value >> 16);
    out[*pos + 3] = (uint8_t)(value >> 24);
    *pos += 4;
}

DxilStatus dxil_write_program_part(uint32_t shader_kind,
    uint32_t shader_model_major, uint32_t shader_model_minor,
    const uint8_t *bitcode, size_t bitcode_len,
    uint8_t *out, size_t out_len, size_t *written) {
    uint64_t padded_bitcode = 0;
    uint64_t total_size = 0;
    uint32_t program_version = 0;
    uint32_t dxil_version = 0;
    size_t pos = 0;
    size_t pad = 0;
    if (bitcode_len > UINT32_MAX) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    padded_bitcode = ((uint64_t)bitcode_len + 3u) & ~UINT64_C(3);
    total_size = (uint64_t)DXIL_PROGRAM_HEADER_SIZE + padded_bitcode;
    if (total_size > out_len) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    program_version = (shader_kind << 16)
        | (shader_model_major << 4) | shader_model_minor;
    write_u32(out, &pos, program_version);
    dxil_version = (DXIL_PROGRAM_MAJOR_VERSION << 8)
        | DXIL_PROGRAM_MINOR_VERSION;
    write_u32(out, &pos, dxil_version);
    write_u32(out, &pos, DXIL_PROGRAM_HEADER_SIZE);
    write_u32(out, &pos, (uint32_t)bitcode_len);
    write_u32(out, &pos, 0);
    write_u32(out, &pos, 0);
    if (bitcode_len > 0) {
        memcpy(out + pos, bitcode, bitcode_len);
    }
    pos += bitcode_len;
    pad = (size_t)padded_bitcode - bitcode_len;
    if (pad > 0) {
        memset(out + pos, 0, pad);
        pos += pad;
    }
    *written = pos;
    return DXIL_OK;
}

DxilStatus dxil_write_container(const DxilPart *parts, size_t part_count,
    uint8_t *out, size_t out_len, size_t *written) {
    uint64_t header_size = 0;
    uint64_t body_size = 0;
    uint64_t total_size = 0;
    size_t pos = 0;
    size_t hash_offset = 0;
    size_t i = 0;
    uint64_t part_data_offset = 0;
    uint8_t digest[16];
    if (part_count > UINT32_MAX) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    header_size = (uint64_t)DXIL_DXBC_HEADER_SIZE + 4u * (uint64_t)part_count;
    for (i = 0; i < part_count; i++) {
        const uint64_t padded = ((uint64_t)parts[i].len + 3u) & ~UINT64_C(3);
        body_size += 8u + padded;
    }
    total_size = header_size + body_size;
    if (total_size > out_len || total_size > UINT32_MAX) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    dxil_dxbc_fourcc_bytes(out + pos);
    pos += 4;
    hash_offset = pos;
    memset(out + pos, 0, DXIL_DXBC_HASH_SIZE);
    pos += DXIL_DXBC_HASH_SIZE;
    write_u32(out, &pos, 1);
    write_u32(out, &pos, (uint32_t)total_size);
    write_u32(out, &pos, (uint32_t)part_count);
    part_data_offset = header_size;
    for (i = 0; i < part_count; i++) {
        const uint64_t padded = ((uint64_t)parts[i].len + 3u) & ~UINT64_C(3);
        write_u32(out, &pos, (uint32_t)part_data_offset);
        part_data_offset += 8u + padded;
    }
    for (i = 0; i < part_count; i++) {
        const size_t remainder = parts[i].len % 4;
        dxil_part_fourcc_bytes(parts[i].fourcc, out + pos);
        pos += 4;
        write_u32(out, &pos, (uint32_t)parts[i].len);
        if (parts[i].len > 0) {
            memcpy(out + pos, parts[i].data, parts[i].len);
        }
        pos += parts[i].len;
        if (remainder != 0) {
            const size_t pad = 4 - remainder;
            memset(out + pos, 0, pad);
            pos += pad;
        }
    }
    if (pos <= 20) {
        memset(out + hash_offset, 0, DXIL_DXBC_HASH_SIZE);
    } else {
        dxil_md5(out + 20, pos - 20, digest);
        memcpy(out + hash_offset, digest, DXIL_DXBC_HASH_SIZE);
    }
    *written = pos;
    return DXIL_OK;
}

DxilStatus dxil_write_empty_signature(uint8_t *out, size_t out_len,
    size_t *written) {
    size_t pos = 0;
    if (8 > out_len) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    write_u32(out, &pos, 0);
    write_u32(out, &pos, 8);
    *written = pos;
    return DXIL_OK;
}

DxilStatus dxil_write_feature_flags(uint8_t *out, size_t out_len,
    size_t *written) {
    size_t pos = 0;
    if (8 > out_len) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    write_u32(out, &pos, 0);
    write_u32(out, &pos, 0);
    *written = pos;
    return DXIL_OK;
}
