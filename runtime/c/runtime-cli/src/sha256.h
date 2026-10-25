/* SPDX-License-Identifier: MIT
 * SHA-256 (FIPS 180-4) for output-oracle digests of read-back buffers. */
#ifndef DOE_RUNTIME_SHA256_H
#define DOE_RUNTIME_SHA256_H
#include <stddef.h>
#include <stdint.h>

void doe_sha256(const void *data, size_t size, uint8_t digest[32]);
/* Lowercase hex, 64 characters plus NUL. */
void doe_sha256_hex(const uint8_t digest[32], char out[65]);
#endif
