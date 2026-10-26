/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_SHA256_H
#define DOE_TSIR_SHA256_H

#include <stddef.h>
#include <stdint.h>

/* Self-contained SHA-256 (FIPS 180-4). No hidden state: the caller owns
 * the context, and tsir_sha256() is a single-shot convenience wrapper.
 * Used by the digest and reference-hash paths; nothing else in this
 * library hashes. */
typedef struct TsirSha256 {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t block[64];
    size_t block_len;
} TsirSha256;

void tsir_sha256_init(TsirSha256 *ctx);
void tsir_sha256_update(TsirSha256 *ctx, const uint8_t *data, size_t len);
void tsir_sha256_final(TsirSha256 *ctx, uint8_t out[32]);
void tsir_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

#endif
