/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_bitcode.h"

#include <string.h>

#include "dxil_spec.h"

void dxil_bitcode_init(DxilBitcodeWriter *writer, uint8_t *buf, size_t len) {
    writer->buf = buf;
    writer->len = len;
    writer->byte_pos = 0;
    writer->bit_pos = 0;
    writer->block_depth = 0;
    writer->current_abbrev_width = 2;
}

size_t dxil_bitcode_position(const DxilBitcodeWriter *writer) {
    return writer->byte_pos;
}

void dxil_bitcode_flush_to_byte_boundary(DxilBitcodeWriter *writer) {
    if (writer->bit_pos != 0) {
        writer->byte_pos += 1;
        writer->bit_pos = 0;
    }
}

static void align_to_32(DxilBitcodeWriter *writer) {
    size_t padding = 0;
    size_t remainder = 0;
    dxil_bitcode_flush_to_byte_boundary(writer);
    remainder = writer->byte_pos % 4;
    if (remainder != 0) {
        padding = 4 - remainder;
        /* Zig skips the padding silently when the buffer has no room;
         * only a later reservation or write reports the shortage. */
        if (writer->byte_pos + padding <= writer->len) {
            memset(writer->buf + writer->byte_pos, 0, padding);
            writer->byte_pos += padding;
        }
    }
}

DxilStatus dxil_bitcode_emit_raw_bytes(DxilBitcodeWriter *writer,
    const uint8_t *data, size_t len) {
    dxil_bitcode_flush_to_byte_boundary(writer);
    if (writer->byte_pos + len > writer->len) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    if (len > 0) {
        memcpy(writer->buf + writer->byte_pos, data, len);
    }
    writer->byte_pos += len;
    return DXIL_OK;
}

DxilStatus dxil_bitcode_emit_bits(DxilBitcodeWriter *writer, uint64_t value,
    unsigned num_bits) {
    unsigned remaining = num_bits;
    uint64_t val = value;
    if (num_bits > 64) {
        return DXIL_INVALID_IR;
    }
    while (remaining > 0) {
        unsigned bits_in_byte = 0;
        unsigned mask = 0;
        unsigned new_bit = 0;
        if (writer->byte_pos >= writer->len) {
            return DXIL_OUTPUT_TOO_LARGE;
        }
        bits_in_byte = remaining < 8u - writer->bit_pos
            ? remaining : 8u - writer->bit_pos;
        mask = (unsigned)((1u << bits_in_byte) - 1u);
        writer->buf[writer->byte_pos] = (uint8_t)(
            (writer->buf[writer->byte_pos] & (uint8_t)~((uint8_t)mask << writer->bit_pos))
            | (uint8_t)((val & mask) << writer->bit_pos));
        val >>= bits_in_byte;
        remaining -= bits_in_byte;
        new_bit = writer->bit_pos + bits_in_byte;
        if (new_bit >= 8) {
            writer->byte_pos += 1;
            writer->bit_pos = 0;
        } else {
            writer->bit_pos = new_bit;
        }
    }
    return DXIL_OK;
}

DxilStatus dxil_bitcode_emit_vbr(DxilBitcodeWriter *writer, uint64_t value,
    unsigned width) {
    unsigned data_bits = 0;
    uint64_t data_mask = 0;
    uint64_t continue_bit = 0;
    uint64_t val = value;
    DxilStatus status = DXIL_OK;
    if (width < 2 || width > 64) {
        return DXIL_INVALID_IR;
    }
    data_bits = width - 1;
    data_mask = (data_bits == 64) ? UINT64_MAX : ((UINT64_C(1) << data_bits) - 1u);
    continue_bit = UINT64_C(1) << data_bits;
    for (;;) {
        const uint64_t chunk = val & data_mask;
        val >>= data_bits;
        if (val == 0) {
            status = dxil_bitcode_emit_bits(writer, chunk, width);
            break;
        }
        status = dxil_bitcode_emit_bits(writer, chunk | continue_bit, width);
        if (status != DXIL_OK) {
            break;
        }
    }
    return status;
}

DxilStatus dxil_bitcode_emit_signed_vbr(DxilBitcodeWriter *writer,
    int64_t value, unsigned width) {
    uint64_t encoded = 0;
    if (value >= 0) {
        encoded = (uint64_t)value << 1;
    } else if (value == INT64_MIN) {
        encoded = UINT64_MAX;
    } else {
        encoded = (((uint64_t)(-(value + 1))) << 1) | 1u;
    }
    return dxil_bitcode_emit_vbr(writer, encoded, width);
}

DxilStatus dxil_bitcode_enter_block(DxilBitcodeWriter *writer,
    uint32_t block_id, unsigned new_abbrev_width) {
    DxilStatus status = DXIL_OK;
    status = dxil_bitcode_emit_bits(writer, DXIL_ABBREV_ENTER_SUBBLOCK,
        writer->current_abbrev_width);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_bitcode_emit_vbr(writer, block_id, 8);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_bitcode_emit_vbr(writer, new_abbrev_width, 4);
    if (status != DXIL_OK) {
        return status;
    }
    align_to_32(writer);
    if (writer->block_depth >= DXIL_BITCODE_MAX_BLOCK_DEPTH) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    writer->block_stack[writer->block_depth].prev_abbrev_width =
        writer->current_abbrev_width;
    writer->block_stack[writer->block_depth].start_byte = writer->byte_pos;
    writer->block_stack[writer->block_depth].start_bit = writer->bit_pos;
    writer->block_stack[writer->block_depth].block_id = block_id;
    writer->block_depth += 1;
    writer->current_abbrev_width = new_abbrev_width;
    if (writer->byte_pos + 4 > writer->len) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memset(writer->buf + writer->byte_pos, 0, 4);
    writer->byte_pos += 4;
    return DXIL_OK;
}

DxilStatus dxil_bitcode_exit_block(DxilBitcodeWriter *writer) {
    DxilStatus status = DXIL_OK;
    DxilBitcodeBlockFrame frame;
    size_t content_start = 0;
    size_t content_bytes = 0;
    uint32_t word_count = 0;
    status = dxil_bitcode_emit_bits(writer, DXIL_ABBREV_END_BLOCK,
        writer->current_abbrev_width);
    if (status != DXIL_OK) {
        return status;
    }
    align_to_32(writer);
    if (writer->block_depth == 0) {
        return DXIL_INVALID_IR;
    }
    writer->block_depth -= 1;
    frame = writer->block_stack[writer->block_depth];
    content_start = frame.start_byte + 4;
    if (writer->byte_pos < content_start) {
        return DXIL_INVALID_IR;
    }
    content_bytes = writer->byte_pos - content_start;
    word_count = (uint32_t)((content_bytes + 3) / 4);
    writer->buf[frame.start_byte] = (uint8_t)word_count;
    writer->buf[frame.start_byte + 1] = (uint8_t)(word_count >> 8);
    writer->buf[frame.start_byte + 2] = (uint8_t)(word_count >> 16);
    writer->buf[frame.start_byte + 3] = (uint8_t)(word_count >> 24);
    writer->current_abbrev_width = frame.prev_abbrev_width;
    return DXIL_OK;
}

DxilStatus dxil_bitcode_emit_record(DxilBitcodeWriter *writer, uint32_t code,
    const uint64_t *operands, size_t operand_count) {
    DxilStatus status = DXIL_OK;
    size_t i = 0;
    status = dxil_bitcode_emit_bits(writer, DXIL_ABBREV_UNABBREV_RECORD,
        writer->current_abbrev_width);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_bitcode_emit_vbr(writer, code, 6);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_bitcode_emit_vbr(writer, (uint64_t)operand_count, 6);
    if (status != DXIL_OK) {
        return status;
    }
    for (i = 0; i < operand_count; i++) {
        status = dxil_bitcode_emit_vbr(writer, operands[i], 6);
        if (status != DXIL_OK) {
            return status;
        }
    }
    return DXIL_OK;
}

DxilStatus dxil_bitcode_emit_record_with_string(DxilBitcodeWriter *writer,
    uint32_t code, const uint64_t *prefix_ops, size_t prefix_count,
    const uint8_t *text, size_t text_len) {
    DxilStatus status = DXIL_OK;
    size_t i = 0;
    status = dxil_bitcode_emit_bits(writer, DXIL_ABBREV_UNABBREV_RECORD,
        writer->current_abbrev_width);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_bitcode_emit_vbr(writer, code, 6);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_bitcode_emit_vbr(writer, (uint64_t)(prefix_count + text_len), 6);
    if (status != DXIL_OK) {
        return status;
    }
    for (i = 0; i < prefix_count; i++) {
        status = dxil_bitcode_emit_vbr(writer, prefix_ops[i], 6);
        if (status != DXIL_OK) {
            return status;
        }
    }
    for (i = 0; i < text_len; i++) {
        status = dxil_bitcode_emit_vbr(writer, text[i], 6);
        if (status != DXIL_OK) {
            return status;
        }
    }
    return DXIL_OK;
}

DxilStatus dxil_bitcode_emit_string_record(DxilBitcodeWriter *writer,
    uint32_t code, const char *text) {
    return dxil_bitcode_emit_record_with_string(writer, code, NULL, 0,
        (const uint8_t *)text, strlen(text));
}

size_t dxil_bitcode_finalize(DxilBitcodeWriter *writer) {
    dxil_bitcode_flush_to_byte_boundary(writer);
    return writer->byte_pos;
}
