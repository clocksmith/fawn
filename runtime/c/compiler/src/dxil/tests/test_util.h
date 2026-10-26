/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_TEST_UTIL_H
#define DOE_DXIL_TEST_UTIL_H

/* Shared test helpers for the DXIL C tests. Not shipped code: a small
 * bit reader (LSB-first, matching the bitcode writer) and CHECK macros
 * that count failures and print one line per check group. */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int dxil_test_failures = 0;
static int dxil_test_checks = 0;

#define CHECK(cond) do { \
    dxil_test_checks++; \
    if (!(cond)) { \
        dxil_test_failures++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_MSG(cond, ...) do { \
    dxil_test_checks++; \
    if (!(cond)) { \
        dxil_test_failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

#define CHECK_STATUS(actual, expected) \
    CHECK_MSG((actual) == (expected), "status %s, expected %s", \
        dxil_status_name(actual), dxil_status_name(expected))

#define TEST_SUMMARY(name) do { \
    if (dxil_test_failures == 0) { \
        printf("PASS %s (%d checks)\n", name, dxil_test_checks); \
    } else { \
        printf("FAIL %s (%d failures of %d checks)\n", name, \
            dxil_test_failures, dxil_test_checks); \
    } \
    return dxil_test_failures == 0 ? 0 : 1; \
} while (0)

typedef struct DxilTestBitReader {
    const uint8_t *buf;
    size_t len;
    size_t byte_pos;
    unsigned bit_pos;
} DxilTestBitReader;

static inline void dxil_test_reader_init(DxilTestBitReader *reader,
    const uint8_t *buf, size_t len) {
    reader->buf = buf;
    reader->len = len;
    reader->byte_pos = 0;
    reader->bit_pos = 0;
}

static inline uint64_t dxil_test_read_bits(DxilTestBitReader *reader,
    unsigned num_bits) {
    uint64_t value = 0;
    unsigned shift = 0;
    unsigned remaining = num_bits;
    while (remaining > 0) {
        unsigned bits_in_byte = remaining < 8u - reader->bit_pos
            ? remaining : 8u - reader->bit_pos;
        unsigned mask = (unsigned)((1u << bits_in_byte) - 1u);
        unsigned chunk = 0;
        if (reader->byte_pos >= reader->len) {
            return value;
        }
        chunk = (unsigned)((reader->buf[reader->byte_pos] >> reader->bit_pos)
            & mask);
        value |= (uint64_t)chunk << shift;
        shift += bits_in_byte;
        remaining -= bits_in_byte;
        reader->bit_pos += bits_in_byte;
        if (reader->bit_pos >= 8) {
            reader->byte_pos += 1;
            reader->bit_pos = 0;
        }
    }
    return value;
}

static inline uint64_t dxil_test_read_vbr(DxilTestBitReader *reader,
    unsigned width) {
    uint64_t value = 0;
    unsigned shift = 0;
    const unsigned data_bits = width - 1;
    for (;;) {
        const uint64_t chunk = dxil_test_read_bits(reader, width);
        value |= (chunk & ((UINT64_C(1) << data_bits) - 1u)) << shift;
        shift += data_bits;
        if ((chunk & (UINT64_C(1) << data_bits)) == 0) {
            break;
        }
    }
    return value;
}

static inline void dxil_test_align32(DxilTestBitReader *reader) {
    if (reader->bit_pos != 0) {
        reader->byte_pos += 1;
        reader->bit_pos = 0;
    }
    while (reader->byte_pos % 4 != 0) {
        reader->byte_pos += 1;
    }
}

static inline uint32_t dxil_test_read_u32le(const uint8_t *buf, size_t offset) {
    return (uint32_t)buf[offset]
        | ((uint32_t)buf[offset + 1] << 8)
        | ((uint32_t)buf[offset + 2] << 16)
        | ((uint32_t)buf[offset + 3] << 24);
}

static inline void dxil_test_write_u32le(uint8_t *buf, size_t offset,
    uint32_t value) {
    buf[offset] = (uint8_t)value;
    buf[offset + 1] = (uint8_t)(value >> 8);
    buf[offset + 2] = (uint8_t)(value >> 16);
    buf[offset + 3] = (uint8_t)(value >> 24);
}

#endif /* DOE_DXIL_TEST_UTIL_H */
