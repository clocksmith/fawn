/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_BITCODE_H
#define DOE_DXIL_BITCODE_H

#include <stddef.h>
#include <stdint.h>

#include "dxil_error.h"

/* LLVM 3.7 bitcode stream writer. Faithful port of dxil_bitcode.zig's
 * Writer: variable-width integers, VBR encoding, block enter/exit with a
 * patched length word, unabbreviated records, and blob data. The writer
 * works on a caller-owned fixed buffer and never allocates.
 *
 * Ownership: the caller owns `buf` for the writer's whole lifetime and
 * must keep it alive until after `dxil_bitcode_finalize`. The writer
 * never frees or retains it beyond that call. Callers zero the buffer
 * first when deterministic padding bits matter, exactly as the Zig tests
 * do. */

#define DXIL_BITCODE_MAX_BLOCK_DEPTH 16u

typedef struct DxilBitcodeBlockFrame {
    unsigned prev_abbrev_width;
    size_t start_byte;
    unsigned start_bit;
    uint32_t block_id;
} DxilBitcodeBlockFrame;

typedef struct DxilBitcodeWriter {
    uint8_t *buf;
    size_t len;
    size_t byte_pos;
    unsigned bit_pos;
    DxilBitcodeBlockFrame block_stack[DXIL_BITCODE_MAX_BLOCK_DEPTH];
    unsigned block_depth;
    unsigned current_abbrev_width;
} DxilBitcodeWriter;

/* Initializes the writer over a caller-owned buffer. The initial abbrev
 * width is 2, matching Zig's INITIAL_ABBREV_WIDTH. */
void dxil_bitcode_init(DxilBitcodeWriter *writer, uint8_t *buf, size_t len);

/* Current byte position (not counting a partial byte). */
size_t dxil_bitcode_position(const DxilBitcodeWriter *writer);

/* Flushes to a byte boundary, then copies the bytes. Fails with
 * DXIL_OUTPUT_TOO_LARGE when the buffer has no room. */
DxilStatus dxil_bitcode_emit_raw_bytes(DxilBitcodeWriter *writer,
    const uint8_t *data, size_t len);

/* Emits the low `num_bits` bits of value, low bits first. Fails with
 * DXIL_OUTPUT_TOO_LARGE when the buffer has no room, and with
 * DXIL_INVALID_IR when num_bits exceeds 64. */
DxilStatus dxil_bitcode_emit_bits(DxilBitcodeWriter *writer, uint64_t value,
    unsigned num_bits);

/* Variable-bit-rate encoding: `width` bits per chunk, `width - 1` data
 * bits, low chunk first, continuation bit on every chunk but the last.
 * Width below 2, or above 64, fails with DXIL_INVALID_IR. */
DxilStatus dxil_bitcode_emit_vbr(DxilBitcodeWriter *writer, uint64_t value,
    unsigned width);

/* Signed VBR: the sign bit is rotated to the LSB (non-negative v encodes
 * as v << 1, negative v as ((-v - 1) << 1) | 1). For INT64_MIN the C
 * port defines the result as UINT64_MAX instead of trapping, which is
 * the one intentional divergence from Zig (see gaps.md). */
DxilStatus dxil_bitcode_emit_signed_vbr(DxilBitcodeWriter *writer,
    int64_t value, unsigned width);

/* Enters a sub-block: emits ENTER_SUBBLOCK in the current width, the
 * block id as VBR width 8, the new width as VBR width 4, aligns to a
 * 32-bit boundary, and reserves a zeroed 4-byte length word that
 * exit_block patches. Nesting deeper than 16 fails with
 * DXIL_OUTPUT_TOO_LARGE. */
DxilStatus dxil_bitcode_enter_block(DxilBitcodeWriter *writer,
    uint32_t block_id, unsigned new_abbrev_width);

/* Emits END_BLOCK, aligns to 32 bits, patches the length word with the
 * content word count, and restores the previous abbrev width. Exiting
 * with no open block fails with DXIL_INVALID_IR, after the END_BLOCK
 * bits and alignment have been written (Zig order, preserved). */
DxilStatus dxil_bitcode_exit_block(DxilBitcodeWriter *writer);

/* Unabbreviated record: UNABBREV_RECORD, VBR code width 6, VBR operand
 * count width 6, then each operand as VBR width 6. */
DxilStatus dxil_bitcode_emit_record(DxilBitcodeWriter *writer, uint32_t code,
    const uint64_t *operands, size_t operand_count);

/* Unabbreviated record whose operand list is the prefix operands
 * followed by one VBR operand per byte of text. */
DxilStatus dxil_bitcode_emit_record_with_string(DxilBitcodeWriter *writer,
    uint32_t code, const uint64_t *prefix_ops, size_t prefix_count,
    const uint8_t *text, size_t text_len);

/* String record: same as above with no prefix operands. */
DxilStatus dxil_bitcode_emit_string_record(DxilBitcodeWriter *writer,
    uint32_t code, const char *text);

/* Closes a partial byte, if any, by advancing one byte. */
void dxil_bitcode_flush_to_byte_boundary(DxilBitcodeWriter *writer);

/* Flushes and returns the final byte position. */
size_t dxil_bitcode_finalize(DxilBitcodeWriter *writer);

#endif /* DOE_DXIL_BITCODE_H */
