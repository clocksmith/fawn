/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include <string.h>

#include "dxil_container.h"
#include "dxil_spec.h"
#include "dxil_validate.h"
#include "test_util.h"

/* Builds a minimal valid container: one DXIL part holding a program
 * part over the 4-byte LLVM magic, mirroring the Zig test helper. */
static size_t build_minimal(uint8_t *buf, size_t buf_len,
    uint32_t shader_kind) {
    uint8_t part_buf[256];
    size_t part_len = 0;
    uint8_t magic[4];
    DxilPart part;
    size_t size = 0;
    DxilStatus status = DXIL_OK;
    dxil_llvm_magic_bytes(magic);
    status = dxil_write_program_part(shader_kind, 6, 0, magic,
        sizeof(magic), part_buf, sizeof(part_buf), &part_len);
    if (status != DXIL_OK) {
        return 0;
    }
    part.fourcc = DXIL_PART_FOURCC_DXIL;
    part.data = part_buf;
    part.len = part_len;
    /* part_buf is stack memory that must outlive the call: copy the
     * part bytes into the tail of buf is not possible here, so this
     * helper writes the container into a caller buffer via a second
     * static-free approach: the container writer copies part data
     * immediately, so part_buf only needs to live during the call. */
    status = dxil_write_container(&part, 1, buf, buf_len, &size);
    if (status != DXIL_OK) {
        return 0;
    }
    return size;
}

/* Independent Microsoft DxilProgramHeader bytes, not emitted by our writer.
 * The optional four-byte gap catches an offset interpreted from the wrong base. */
static void check_microsoft_program_header(void) {
    const uint8_t program[32] = {
        0x60, 0x00, 0x05, 0x00, 0x08, 0x00, 0x00, 0x00,
        'D', 'X', 'I', 'L', 0x00, 0x01, 0x00, 0x00,
        0x14, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 'B', 'C', 0xC0, 0xDE
    };
    const struct {
        size_t offset;
        uint32_t value;
        DxilValidationError error;
    } corruptions[] = {
        {4, 5, DXIL_VALIDATION_DXIL_PART_TOO_SMALL},
        {4, UINT32_MAX, DXIL_VALIDATION_DXIL_PART_TOO_SMALL},
        {4, 7, DXIL_VALIDATION_DXIL_PART_TOO_SMALL},
        {8, 0, DXIL_VALIDATION_BAD_MAGIC},
        {16, 0, DXIL_VALIDATION_DXIL_PART_TOO_SMALL},
        {16, UINT32_MAX - 7u, DXIL_VALIDATION_DXIL_PART_TOO_SMALL},
        {20, 0, DXIL_VALIDATION_DXIL_PART_TOO_SMALL},
        {20, 3, DXIL_VALIDATION_DXIL_PART_TOO_SMALL},
        {28, 0, DXIL_VALIDATION_BAD_BITCODE_MAGIC}
    };
    uint8_t container[128];
    uint8_t changed[sizeof(program)];
    DxilPart part = {DXIL_PART_FOURCC_DXIL, program, sizeof(program)};
    DxilValidationResult result;
    size_t written = 0;
    size_t i = 0;
    CHECK_STATUS(dxil_write_container(&part, 1, container,
        sizeof(container), &written), DXIL_OK);
    dxil_validate(container, written, &result);
    CHECK(result.valid);
    CHECK(result.bitcode_size == 4);
    CHECK(result.shader_kind == DXIL_SHADER_COMPUTE);
    CHECK(result.dxil_major == 6 && result.dxil_minor == 0);
    for (i = 0; i < sizeof(corruptions) / sizeof(corruptions[0]); i++) {
        memcpy(changed, program, sizeof(changed));
        dxil_test_write_u32le(changed, corruptions[i].offset,
            corruptions[i].value);
        part.data = changed;
        CHECK_STATUS(dxil_write_container(&part, 1, container,
            sizeof(container), &written), DXIL_OK);
        dxil_validate(container, written, &result);
        CHECK(!result.valid);
        CHECK(result.error == corruptions[i].error);
        CHECK(!result.has_bitcode_size && !result.has_dxil_part);
    }
    /* Reject the inherited private six-word layout rather than accepting a
     * second format that Microsoft DXC cannot consume. */
    memcpy(changed, program, sizeof(changed));
    dxil_test_write_u32le(changed, 4, 0x100);
    dxil_test_write_u32le(changed, 8, 24);
    part.data = changed;
    CHECK_STATUS(dxil_write_container(&part, 1, container,
        sizeof(container), &written), DXIL_OK);
    dxil_validate(container, written, &result);
    CHECK(!result.valid);
    CHECK(result.error == DXIL_VALIDATION_BAD_MAGIC);
}

int main(void) {
    check_microsoft_program_header();
    /* Zig test parity: valid minimal container. */
    {
        uint8_t buf[1024];
        size_t size = 0;
        DxilValidationResult result;
        memset(buf, 0, sizeof(buf));
        size = build_minimal(buf, sizeof(buf), DXIL_SHADER_COMPUTE);
        CHECK(size > 0);
        dxil_validate(buf, size, &result);
        CHECK(result.valid);
        CHECK(result.container_size == (uint32_t)size);
        CHECK(result.part_count == 1);
        CHECK(result.has_dxil_part);
        CHECK(result.has_shader_kind);
        CHECK(result.shader_kind == DXIL_SHADER_COMPUTE);
        CHECK(result.has_bitcode_size);
        CHECK(result.bitcode_size >= 4);
        CHECK(result.error_message == NULL);
        CHECK(result.error == DXIL_VALIDATION_NONE);
    }

    /* Zig test parity: too-small data. */
    {
        const uint8_t tiny[8] = { 'D', 'X', 'B', 'C', 0, 0, 0, 0 };
        DxilValidationResult result;
        dxil_validate(tiny, sizeof(tiny), &result);
        CHECK(!result.valid);
        CHECK(result.error_message != NULL);
        CHECK(strstr(result.error_message, "too small") != NULL);
        CHECK(result.error == DXIL_VALIDATION_TOO_SMALL);
    }

    /* Zig test parity: bad magic. */
    {
        uint8_t buf[1024];
        size_t size = 0;
        DxilValidationResult result;
        memset(buf, 0, sizeof(buf));
        size = build_minimal(buf, sizeof(buf), DXIL_SHADER_COMPUTE);
        buf[0] = 'X';
        dxil_validate(buf, size, &result);
        CHECK(!result.valid);
        CHECK(strstr(result.error_message, "magic") != NULL);
        CHECK(result.error == DXIL_VALIDATION_BAD_MAGIC);
        /* Failure results report no DXIL part, even though one exists. */
        CHECK(!result.has_dxil_part);
        CHECK(!result.has_shader_kind);
    }

    /* Zig test parity: bad version. */
    {
        uint8_t buf[1024];
        size_t size = 0;
        DxilValidationResult result;
        memset(buf, 0, sizeof(buf));
        size = build_minimal(buf, sizeof(buf), DXIL_SHADER_COMPUTE);
        dxil_test_write_u32le(buf, 20, 2);
        dxil_validate(buf, size, &result);
        CHECK(!result.valid);
        CHECK(strstr(result.error_message, "version") != NULL);
        CHECK(result.error == DXIL_VALIDATION_BAD_VERSION);
    }

    /* Zig test parity: size mismatch. */
    {
        uint8_t buf[1024];
        size_t size = 0;
        DxilValidationResult result;
        memset(buf, 0, sizeof(buf));
        size = build_minimal(buf, sizeof(buf), DXIL_SHADER_COMPUTE);
        dxil_test_write_u32le(buf, 24, (uint32_t)size + 100);
        dxil_validate(buf, size, &result);
        CHECK(!result.valid);
        CHECK(strstr(result.error_message, "size") != NULL);
        CHECK(result.error == DXIL_VALIDATION_SIZE_MISMATCH);
        CHECK(result.container_size == (uint32_t)size + 100);
    }

    /* Zig test parity: container with a HASH part. */
    {
        uint8_t buf[1024];
        uint8_t part_buf[256];
        size_t part_len = 0;
        uint8_t magic[4];
        uint8_t hash_data[20];
        DxilPart parts[2];
        size_t size = 0;
        DxilValidationResult result;
        memset(buf, 0, sizeof(buf));
        memset(hash_data, 0, sizeof(hash_data));
        dxil_llvm_magic_bytes(magic);
        CHECK_STATUS(dxil_write_program_part(DXIL_SHADER_VERTEX, 6, 0,
            magic, sizeof(magic), part_buf, sizeof(part_buf),
            &part_len), DXIL_OK);
        parts[0].fourcc = DXIL_PART_FOURCC_DXIL;
        parts[0].data = part_buf;
        parts[0].len = part_len;
        parts[1].fourcc = DXIL_PART_FOURCC_HASH;
        parts[1].data = hash_data;
        parts[1].len = sizeof(hash_data);
        CHECK_STATUS(dxil_write_container(parts, 2, buf, sizeof(buf),
            &size), DXIL_OK);
        dxil_validate(buf, size, &result);
        CHECK(result.valid);
        CHECK(result.has_hash_part);
        CHECK(result.part_count == 2);
        CHECK(result.shader_kind == DXIL_SHADER_VERTEX);
    }

    /* Part count 256 is rejected (>= 256). */
    {
        uint8_t buf[64];
        DxilValidationResult result;
        memset(buf, 0, sizeof(buf));
        memcpy(buf, "DXBC", 4);
        dxil_test_write_u32le(buf, 20, 1);
        dxil_test_write_u32le(buf, 24, 64);
        dxil_test_write_u32le(buf, 28, 256);
        dxil_validate(buf, sizeof(buf), &result);
        CHECK(!result.valid);
        CHECK(result.error == DXIL_VALIDATION_PART_COUNT_TOO_LARGE);
        CHECK(strstr(result.error_message, "256") != NULL);
    }

    /* A container with only an ISGN part has no DXIL part. */
    {
        uint8_t buf[256];
        uint8_t sig[8];
        size_t sig_len = 0;
        DxilPart part;
        size_t size = 0;
        DxilValidationResult result;
        CHECK_STATUS(dxil_write_empty_signature(sig, sizeof(sig),
            &sig_len), DXIL_OK);
        part.fourcc = DXIL_PART_FOURCC_ISGN;
        part.data = sig;
        part.len = sig_len;
        CHECK_STATUS(dxil_write_container(&part, 1, buf, sizeof(buf),
            &size), DXIL_OK);
        dxil_validate(buf, size, &result);
        CHECK(!result.valid);
        CHECK(result.error == DXIL_VALIDATION_NO_DXIL_PART);
    }

    /* A DXIL part shorter than the 24-byte program header. */
    {
        uint8_t buf[256];
        const uint8_t short_part[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        DxilPart part;
        size_t size = 0;
        DxilValidationResult result;
        part.fourcc = DXIL_PART_FOURCC_DXIL;
        part.data = short_part;
        part.len = sizeof(short_part);
        CHECK_STATUS(dxil_write_container(&part, 1, buf, sizeof(buf),
            &size), DXIL_OK);
        dxil_validate(buf, size, &result);
        CHECK(!result.valid);
        CHECK(result.error == DXIL_VALIDATION_DXIL_PART_TOO_SMALL);
    }

    /* A DXIL part whose bitcode is not the LLVM magic. */
    {
        uint8_t buf[256];
        uint8_t part_buf[256];
        size_t part_len = 0;
        const uint8_t bad_bitcode[4] = { 0, 0, 0, 0 };
        DxilPart part;
        size_t size = 0;
        DxilValidationResult result;
        CHECK_STATUS(dxil_write_program_part(DXIL_SHADER_COMPUTE, 6, 0,
            bad_bitcode, sizeof(bad_bitcode), part_buf,
            sizeof(part_buf), &part_len), DXIL_OK);
        part.fourcc = DXIL_PART_FOURCC_DXIL;
        part.data = part_buf;
        part.len = part_len;
        CHECK_STATUS(dxil_write_container(&part, 1, buf, sizeof(buf),
            &size), DXIL_OK);
        dxil_validate(buf, size, &result);
        CHECK(!result.valid);
        CHECK(result.error == DXIL_VALIDATION_BAD_BITCODE_MAGIC);
    }

    /* A part offset pointing past the end of the container. */
    {
        uint8_t buf[64];
        DxilValidationResult result;
        memset(buf, 0, sizeof(buf));
        memcpy(buf, "DXBC", 4);
        dxil_test_write_u32le(buf, 20, 1);
        dxil_test_write_u32le(buf, 24, 64);
        dxil_test_write_u32le(buf, 28, 1);
        dxil_test_write_u32le(buf, 32, 9999);
        dxil_validate(buf, sizeof(buf), &result);
        CHECK(!result.valid);
        CHECK(result.error
            == DXIL_VALIDATION_PART_OFFSET_OUT_OF_BOUNDS);
    }

    /* Error-result size/part-count rules for a 30-byte input. */
    {
        uint8_t buf[30];
        DxilValidationResult result;
        memset(buf, 0, sizeof(buf));
        memcpy(buf, "DXBC", 4);
        dxil_test_write_u32le(buf, 24, 1234);
        dxil_validate(buf, sizeof(buf), &result);
        CHECK(!result.valid);
        CHECK(result.error == DXIL_VALIDATION_TOO_SMALL);
        CHECK(result.container_size == 1234);
        CHECK(result.part_count == 0);
    }

    TEST_SUMMARY("test_dxil_validate");
}
