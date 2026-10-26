/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include <string.h>

#include "dxil_bitcode.h"
#include "dxil_spec.h"
#include "test_util.h"

int main(void) {
    /* Zig test parity: magic + MODULE block + VERSION record. */
    {
        uint8_t buf[256];
        uint8_t magic[4];
        DxilBitcodeWriter writer;
        DxilStatus status = DXIL_OK;
        size_t size = 0;
        memset(buf, 0, sizeof(buf));
        dxil_bitcode_init(&writer, buf, sizeof(buf));
        dxil_llvm_magic_bytes(magic);
        status = dxil_bitcode_emit_raw_bytes(&writer, magic, 4);
        CHECK_STATUS(status, DXIL_OK);
        CHECK(dxil_bitcode_position(&writer) == 4);
        status = dxil_bitcode_enter_block(&writer, DXIL_BLOCK_MODULE, 3);
        CHECK_STATUS(status, DXIL_OK);
        status = dxil_bitcode_emit_record(&writer, DXIL_MODULE_VERSION,
            (const uint64_t[]){ 1 }, 1);
        CHECK_STATUS(status, DXIL_OK);
        status = dxil_bitcode_exit_block(&writer);
        CHECK_STATUS(status, DXIL_OK);
        size = dxil_bitcode_finalize(&writer);
        CHECK(size > 4);
        CHECK(memcmp(buf, magic, 4) == 0);
    }

    /* VBR exact chunk bits at width 4 (3 data bits per chunk). */
    {
        uint8_t buf[16];
        DxilBitcodeWriter writer;
        memset(buf, 0, sizeof(buf));
        dxil_bitcode_init(&writer, buf, sizeof(buf));
        CHECK_STATUS(dxil_bitcode_emit_vbr(&writer, 0, 4), DXIL_OK);
        CHECK_STATUS(dxil_bitcode_emit_vbr(&writer, 5, 4), DXIL_OK);
        CHECK_STATUS(dxil_bitcode_emit_vbr(&writer, 7, 4), DXIL_OK);
        CHECK_STATUS(dxil_bitcode_emit_vbr(&writer, 8, 4), DXIL_OK);
        /* Bits, LSB first: 0000 0101 0111, then chunk 1000, chunk 0001.
         * Byte 0 = 0x50, byte 1 = 0x87, byte 2 = 0x01. */
        CHECK(buf[0] == 0x50);
        CHECK(buf[1] == 0x87);
        CHECK(buf[2] == 0x01);
    }

    /* Signed VBR rotation: -1 -> 1, 2 -> 4, -2 -> 3, 0 -> 0. */
    {
        uint8_t buf[16];
        DxilBitcodeWriter writer;
        DxilTestBitReader reader;
        memset(buf, 0, sizeof(buf));
        dxil_bitcode_init(&writer, buf, sizeof(buf));
        CHECK_STATUS(dxil_bitcode_emit_signed_vbr(&writer, -1, 6), DXIL_OK);
        CHECK_STATUS(dxil_bitcode_emit_signed_vbr(&writer, 2, 6), DXIL_OK);
        CHECK_STATUS(dxil_bitcode_emit_signed_vbr(&writer, -2, 6), DXIL_OK);
        CHECK_STATUS(dxil_bitcode_emit_signed_vbr(&writer, 0, 6), DXIL_OK);
        dxil_test_reader_init(&reader, buf, sizeof(buf));
        CHECK(dxil_test_read_vbr(&reader, 6) == 1);
        CHECK(dxil_test_read_vbr(&reader, 6) == 4);
        CHECK(dxil_test_read_vbr(&reader, 6) == 3);
        CHECK(dxil_test_read_vbr(&reader, 6) == 0);
    }

    /* emit_bits preserves bits outside the written field and crosses
     * byte boundaries LSB-first. */
    {
        uint8_t buf[4];
        DxilBitcodeWriter writer;
        buf[0] = 0xFF;
        buf[1] = 0x00;
        buf[2] = 0x00;
        buf[3] = 0x00;
        dxil_bitcode_init(&writer, buf, sizeof(buf));
        CHECK_STATUS(dxil_bitcode_emit_bits(&writer, 0x5, 3), DXIL_OK);
        CHECK(buf[0] == 0xFD);
        memset(buf, 0, sizeof(buf));
        dxil_bitcode_init(&writer, buf, sizeof(buf));
        CHECK_STATUS(dxil_bitcode_emit_bits(&writer, 0xF, 4), DXIL_OK);
        CHECK_STATUS(dxil_bitcode_emit_bits(&writer, 0xAB, 8), DXIL_OK);
        CHECK(buf[0] == 0xBF);
        CHECK(buf[1] == 0x0A);
    }

    /* Block length word is patched to the content word count. Layout:
     * enter at byte 0 (2 + 8 + 4 = 14 bits, aligned to 4), length word
     * at bytes 4..8, one VERSION record (21 bits from byte 8), END
     * block, aligned to 12. Content is 4 bytes, so the word is 1. */
    {
        uint8_t buf[64];
        DxilBitcodeWriter writer;
        size_t size = 0;
        memset(buf, 0, sizeof(buf));
        dxil_bitcode_init(&writer, buf, sizeof(buf));
        CHECK_STATUS(dxil_bitcode_enter_block(&writer, DXIL_BLOCK_MODULE,
            3), DXIL_OK);
        CHECK_STATUS(dxil_bitcode_emit_record(&writer, DXIL_MODULE_VERSION,
            (const uint64_t[]){ 1 }, 1), DXIL_OK);
        CHECK_STATUS(dxil_bitcode_exit_block(&writer), DXIL_OK);
        size = dxil_bitcode_finalize(&writer);
        CHECK(dxil_test_read_u32le(buf, 4) == 1);
        CHECK(size == 12);
        /* Abbrev width is restored: a second top-level block enter
         * must again use width 2 for its ENTER code. */
        CHECK_STATUS(dxil_bitcode_enter_block(&writer, DXIL_BLOCK_TYPE,
            4), DXIL_OK);
        CHECK_STATUS(dxil_bitcode_exit_block(&writer), DXIL_OK);
    }

    /* Error paths. */
    {
        uint8_t buf[64];
        DxilBitcodeWriter writer;
        memset(buf, 0, sizeof(buf));
        dxil_bitcode_init(&writer, buf, sizeof(buf));
        CHECK_STATUS(dxil_bitcode_exit_block(&writer), DXIL_INVALID_IR);
        CHECK_STATUS(dxil_bitcode_emit_vbr(&writer, 1, 1), DXIL_INVALID_IR);
        CHECK_STATUS(dxil_bitcode_emit_vbr(&writer, 1, 0), DXIL_INVALID_IR);
        CHECK_STATUS(dxil_bitcode_emit_vbr(&writer, 1, 65), DXIL_INVALID_IR);
    }
    {
        uint8_t buf[2];
        DxilBitcodeWriter writer;
        const uint8_t data[4] = { 1, 2, 3, 4 };
        dxil_bitcode_init(&writer, buf, sizeof(buf));
        CHECK_STATUS(dxil_bitcode_emit_raw_bytes(&writer, data, 4),
            DXIL_OUTPUT_TOO_LARGE);
    }
    {
        /* Nesting depth: 16 nested enters succeed, the 17th fails. */
        uint8_t buf[4096];
        DxilBitcodeWriter writer;
        unsigned i = 0;
        memset(buf, 0, sizeof(buf));
        dxil_bitcode_init(&writer, buf, sizeof(buf));
        for (i = 0; i < 16; i++) {
            CHECK_STATUS(dxil_bitcode_enter_block(&writer,
                DXIL_BLOCK_MODULE, 3), DXIL_OK);
        }
        CHECK_STATUS(dxil_bitcode_enter_block(&writer, DXIL_BLOCK_MODULE,
            3), DXIL_OUTPUT_TOO_LARGE);
    }

    /* Record with string: code, count = prefix + text, then operands. */
    {
        uint8_t buf[64];
        DxilBitcodeWriter writer;
        DxilTestBitReader reader;
        const uint64_t prefix[1] = { 7 };
        memset(buf, 0, sizeof(buf));
        dxil_bitcode_init(&writer, buf, sizeof(buf));
        CHECK_STATUS(dxil_bitcode_emit_record_with_string(&writer, 2,
            prefix, 1, (const uint8_t *)"AB", 2), DXIL_OK);
        dxil_test_reader_init(&reader, buf, sizeof(buf));
        CHECK(dxil_test_read_bits(&reader, 2) == 3);
        CHECK(dxil_test_read_vbr(&reader, 6) == 2);
        CHECK(dxil_test_read_vbr(&reader, 6) == 3);
        CHECK(dxil_test_read_vbr(&reader, 6) == 7);
        CHECK(dxil_test_read_vbr(&reader, 6) == 'A');
        CHECK(dxil_test_read_vbr(&reader, 6) == 'B');
    }

    TEST_SUMMARY("test_dxil_bitcode");
}
