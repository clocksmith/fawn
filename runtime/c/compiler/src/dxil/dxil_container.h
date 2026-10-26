/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_CONTAINER_H
#define DOE_DXIL_CONTAINER_H

#include <stddef.h>
#include <stdint.h>

#include "dxil_error.h"
#include "dxil_spec.h"

/* DXBC container format writer. Faithful port of dxil_container.zig:
 * a DXBC envelope around pre-assembled parts, a DXIL program header
 * around bitcode, a minimal empty I/O signature, and a zero feature
 * flags part. All outputs go into caller-owned buffers; every function
 * fails with DXIL_OUTPUT_TOO_LARGE when the buffer is too small and
 * never writes past its end. */

typedef struct DxilPart {
    DxilPartFourcc fourcc;
    const uint8_t *data;
    size_t len;
} DxilPart;

/* Encodes Microsoft's DXIL program header plus bitcode: program version,
 * total size in words, DXIL magic, DXIL version 1.0, bitcode offset 16
 * relative to that magic, and unpadded bitcode size. Bitcode follows the
 * 24-byte program header, padded with zeros to a 4-byte boundary. */
DxilStatus dxil_write_program_part(uint32_t shader_kind,
    uint32_t shader_model_major, uint32_t shader_model_minor,
    const uint8_t *bitcode, size_t bitcode_len,
    uint8_t *out, size_t out_len, size_t *written);

/* Writes a complete DXBC container: magic, 16-byte hash field (filled
 * last, see below), version 1, total size, part count, the offset
 * table, then each part as fourcc, unpadded size, data, zero padding.
 * The hash is the MD5 of the container bytes starting at offset 20
 * (after magic plus hash field). Note the Zig comment claims the first
 * four hash bytes are XORed with the container size, but the Zig code
 * does no XOR; this port implements the code, plain MD5. */
DxilStatus dxil_write_container(const DxilPart *parts, size_t part_count,
    uint8_t *out, size_t out_len, size_t *written);

/* Minimal I/O signature part (ISGN or OSGN): exactly 8 bytes, element
 * count 0 and header byte size 8. */
DxilStatus dxil_write_empty_signature(uint8_t *out, size_t out_len,
    size_t *written);

/* Feature flags part (SFI0): exactly 8 zero bytes. */
DxilStatus dxil_write_feature_flags(uint8_t *out, size_t out_len,
    size_t *written);

#endif /* DOE_DXIL_CONTAINER_H */
