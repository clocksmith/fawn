/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include <string.h>

#include "dxil_container.h"
#include "dxil_md5.h"
#include "dxil_spec.h"
#include "test_util.h"

static void check_hash_hex(const uint8_t *hash, const char *expected,
    const char *label) {
    static const char digits[] = "0123456789abcdef";
    char actual[33];
    int i = 0;
    for (i = 0; i < 16; i++) {
        actual[i * 2] = digits[hash[i] >> 4];
        actual[i * 2 + 1] = digits[hash[i] & 0xFu];
    }
    actual[32] = '\0';
    CHECK_MSG(strcmp(actual, expected) == 0, "%s: got %s, expected %s",
        label, actual, expected);
}

int main(void) {
    /* Zig test parity: empty container has valid structure. */
    {
        uint8_t buf[256];
        size_t size = 0;
        uint8_t magic[4];
        memset(buf, 0, sizeof(buf));
        CHECK_STATUS(dxil_write_container(NULL, 0, buf, sizeof(buf),
            &size), DXIL_OK);
        CHECK(size >= DXIL_DXBC_HEADER_SIZE);
        dxil_dxbc_fourcc_bytes(magic);
        CHECK(memcmp(buf, magic, 4) == 0);
        CHECK(dxil_test_read_u32le(buf, 20) == 1);
        CHECK(dxil_test_read_u32le(buf, 24) == (uint32_t)size);
        CHECK(dxil_test_read_u32le(buf, 28) == 0);
        CHECK(size == 32);
        /* Frozen oracle: MD5 of bytes 20..32 of the empty container,
         * computed with Python hashlib during development. */
        check_hash_hex(buf + 4, "339ca4aa7ccd6132b420000b0a09afc0",
            "empty container hash");
    }

    /* Zig test parity: one-part container round-trips. */
    {
        uint8_t buf[1024];
        size_t size = 0;
        const uint8_t data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        DxilPart parts[1];
        uint8_t digest[16];
        parts[0].fourcc = DXIL_PART_FOURCC_DXIL;
        parts[0].data = data;
        parts[0].len = sizeof(data);
        memset(buf, 0, sizeof(buf));
        CHECK_STATUS(dxil_write_container(parts, 1, buf, sizeof(buf),
            &size), DXIL_OK);
        CHECK(size > DXIL_DXBC_HEADER_SIZE);
        CHECK(dxil_test_read_u32le(buf, 28) == 1);
        CHECK(dxil_test_read_u32le(buf, 32) == 36);
        CHECK(dxil_test_read_u32le(buf, 24) == (uint32_t)size);
        CHECK(size == 48);
        CHECK(memcmp(buf + 36, "DXIL", 4) == 0);
        CHECK(dxil_test_read_u32le(buf, 40) == 4);
        CHECK(memcmp(buf + 44, data, 4) == 0);
        /* Hash covers bytes from offset 20 and sits at bytes 4..20. */
        dxil_md5(buf + 20, size - 20, digest);
        CHECK(memcmp(buf + 4, digest, 16) == 0);
    }

    /* Hash placement: changing a body byte changes the hash. */
    {
        uint8_t buf_a[256];
        uint8_t buf_b[256];
        size_t size_a = 0;
        size_t size_b = 0;
        const uint8_t data_a[1] = { 0x01 };
        const uint8_t data_b[1] = { 0x02 };
        DxilPart part_a;
        DxilPart part_b;
        part_a.fourcc = DXIL_PART_FOURCC_SFI0;
        part_a.data = data_a;
        part_a.len = 1;
        part_b.fourcc = DXIL_PART_FOURCC_SFI0;
        part_b.data = data_b;
        part_b.len = 1;
        CHECK_STATUS(dxil_write_container(&part_a, 1, buf_a,
            sizeof(buf_a), &size_a), DXIL_OK);
        CHECK_STATUS(dxil_write_container(&part_b, 1, buf_b,
            sizeof(buf_b), &size_b), DXIL_OK);
        CHECK(size_a == size_b);
        CHECK(memcmp(buf_a + 4, buf_b + 4, 16) != 0);
        /* The 1-byte part pads to 4: total 36 + 8 + 4 = 48. */
        CHECK(size_a == 48);
        CHECK(buf_a[44] == 0x01);
        CHECK(buf_a[45] == 0);
        CHECK(buf_a[46] == 0);
        CHECK(buf_a[47] == 0);
    }

    /* Program part layout: compute, SM 6.0, 4-byte bitcode. */
    {
        uint8_t buf[256];
        size_t size = 0;
        const uint8_t bitcode[4] = { 'B', 'C', 0xC0, 0xDE };
        CHECK_STATUS(dxil_write_program_part(DXIL_SHADER_COMPUTE, 6, 0,
            bitcode, sizeof(bitcode), buf, sizeof(buf), &size), DXIL_OK);
        CHECK(size == 28);
        CHECK(dxil_test_read_u32le(buf, 0) == 0x00050060u);
        CHECK(dxil_test_read_u32le(buf, 4) == 7);
        CHECK(memcmp(buf + 8, "DXIL", 4) == 0);
        CHECK(dxil_test_read_u32le(buf, 12) == 0x00000100u);
        CHECK(dxil_test_read_u32le(buf, 16) == 16);
        CHECK(dxil_test_read_u32le(buf, 20) == 4);
        CHECK(memcmp(buf + 24, bitcode, 4) == 0);
    }

    /* Program part padding: 5-byte bitcode pads with 3 zero bytes. */
    {
        uint8_t buf[256];
        size_t size = 0;
        const uint8_t bitcode[5] = { 1, 2, 3, 4, 5 };
        CHECK_STATUS(dxil_write_program_part(DXIL_SHADER_VERTEX, 6, 0,
            bitcode, sizeof(bitcode), buf, sizeof(buf), &size), DXIL_OK);
        CHECK(size == 32);
        CHECK(dxil_test_read_u32le(buf, 0) == 0x00010060u);
        CHECK(dxil_test_read_u32le(buf, 20) == 5);
        CHECK(buf[29] == 0);
        CHECK(buf[30] == 0);
        CHECK(buf[31] == 0);
    }

    /* Empty signature and feature flags parts. */
    {
        uint8_t buf[16];
        size_t size = 0;
        CHECK_STATUS(dxil_write_empty_signature(buf, sizeof(buf),
            &size), DXIL_OK);
        CHECK(size == 8);
        CHECK(dxil_test_read_u32le(buf, 0) == 0);
        CHECK(dxil_test_read_u32le(buf, 4) == 8);
        CHECK_STATUS(dxil_write_feature_flags(buf, sizeof(buf),
            &size), DXIL_OK);
        CHECK(size == 8);
        CHECK(dxil_test_read_u32le(buf, 0) == 0);
        CHECK(dxil_test_read_u32le(buf, 4) == 0);
    }

    /* Output-too-large paths. */
    {
        uint8_t buf[8];
        size_t size = 0;
        const uint8_t data[4] = { 1, 2, 3, 4 };
        DxilPart part;
        part.fourcc = DXIL_PART_FOURCC_DXIL;
        part.data = data;
        part.len = sizeof(data);
        CHECK_STATUS(dxil_write_container(&part, 1, buf, sizeof(buf),
            &size), DXIL_OUTPUT_TOO_LARGE);
        CHECK_STATUS(dxil_write_program_part(DXIL_SHADER_COMPUTE, 6, 0,
            data, sizeof(data), buf, sizeof(buf), &size),
            DXIL_OUTPUT_TOO_LARGE);
        CHECK_STATUS(dxil_write_empty_signature(buf, 4, &size),
            DXIL_OUTPUT_TOO_LARGE);
        CHECK_STATUS(dxil_write_feature_flags(buf, 4, &size),
            DXIL_OUTPUT_TOO_LARGE);
    }

    TEST_SUMMARY("test_dxil_container");
}
