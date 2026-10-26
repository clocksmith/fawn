/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_TEST_COMMON_H
#define DOE_TSIR_TEST_COMMON_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_failures = 0;
static int test_checks = 0;

#define CHECK(cond) do { \
        test_checks += 1; \
        if (!(cond)) { \
            test_failures += 1; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_STATUS(got, want) do { \
        test_checks += 1; \
        if ((got) != (want)) { \
            test_failures += 1; \
            printf("FAIL %s:%d: status got %d want %d\n", __FILE__, \
                __LINE__, (int)(got), (int)(want)); \
        } \
    } while (0)

#define CHECK_STR(got, want) do { \
        test_checks += 1; \
        if ((got) == NULL || strcmp((got), (want)) != 0) { \
            test_failures += 1; \
            printf("FAIL %s:%d:\n  got:  %s\n  want: %s\n", __FILE__, \
                __LINE__, (got) == NULL ? "(null)" : (got), (want)); \
        } \
    } while (0)

static inline int finish_tests(const char *name) {
    printf("%s: %d checks, %d failures\n", name, test_checks,
        test_failures);
    return test_failures == 0 ? 0 : 1;
}

static inline void put_f32(uint8_t *buf, size_t index, float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    buf[index * 4] = (uint8_t)(bits & 0xffu);
    buf[index * 4 + 1] = (uint8_t)((bits >> 8) & 0xffu);
    buf[index * 4 + 2] = (uint8_t)((bits >> 16) & 0xffu);
    buf[index * 4 + 3] = (uint8_t)((bits >> 24) & 0xffu);
}

static inline float get_f32(const uint8_t *buf, size_t index) {
    uint32_t bits = (uint32_t)buf[index * 4] |
        ((uint32_t)buf[index * 4 + 1] << 8) |
        ((uint32_t)buf[index * 4 + 2] << 16) |
        ((uint32_t)buf[index * 4 + 3] << 24);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline void hex_digest(const uint8_t digest[32], char out[65]) {
    static const char hex[] = "0123456789abcdef";
    size_t i = 0;
    for (i = 0; i < 32; i++) {
        out[i * 2] = hex[(digest[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    out[64] = '\0';
}

#endif
