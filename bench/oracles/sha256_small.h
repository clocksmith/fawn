/* SPDX-License-Identifier: MIT
 * SHA-256 for branch outputs: CommonCrypto on Apple, a portable FIPS 180-4
 * implementation elsewhere. Shared by the snapshot-branch oracle and executor so
 * both hash the same byte stream the same way; it computes no expected state. */
#ifndef BENCH_SHA256_SMALL_H
#define BENCH_SHA256_SMALL_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__APPLE__) && !defined(SHA256_PORTABLE)
#include <CommonCrypto/CommonDigest.h>
typedef CC_SHA256_CTX sha256_ctx;
static inline void sha256_init(sha256_ctx *c) { CC_SHA256_Init(c); }
static inline void sha256_update(sha256_ctx *c, const void *data, size_t n)
{
    const uint8_t *p = data;
    while (n) {
        CC_LONG part = n > 0x40000000u ? 0x40000000u : (CC_LONG)n;
        CC_SHA256_Update(c, p, part);
        p += part;
        n -= part;
    }
}
static inline void sha256_final(sha256_ctx *c, uint8_t out[32]) { CC_SHA256_Final(out, c); }
#else
typedef struct sha256_ctx { uint32_t h[8]; uint64_t bytes; uint8_t block[64]; size_t used; } sha256_ctx;
static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define SHA256_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static inline void sha256_block(sha256_ctx *c, const uint8_t *p)
{
    uint32_t w[64], s[8];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = SHA256_ROR(w[i - 15], 7) ^ SHA256_ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = SHA256_ROR(w[i - 2], 17) ^ SHA256_ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(s, c->h, sizeof(s));
    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = s[7] + (SHA256_ROR(s[4], 6) ^ SHA256_ROR(s[4], 11) ^ SHA256_ROR(s[4], 25)) +
            ((s[4] & s[5]) ^ (~s[4] & s[6])) + sha256_k[i] + w[i];
        uint32_t t2 = (SHA256_ROR(s[0], 2) ^ SHA256_ROR(s[0], 13) ^ SHA256_ROR(s[0], 22)) +
            ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
        memmove(s + 1, s, 7 * sizeof(uint32_t));
        s[4] += t1;
        s[0] = t1 + t2;
    }
    for (int i = 0; i < 8; ++i) c->h[i] += s[i];
}
static inline void sha256_init(sha256_ctx *c)
{
    static const uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(c->h, h, sizeof(h));
    c->bytes = 0;
    c->used = 0;
}
static inline void sha256_update(sha256_ctx *c, const void *data, size_t n)
{
    const uint8_t *p = data;
    c->bytes += n;
    while (n) {
        if (!c->used && n >= 64) { sha256_block(c, p); p += 64; n -= 64; continue; }
        size_t part = 64 - c->used < n ? 64 - c->used : n;
        memcpy(c->block + c->used, p, part);
        c->used += part; p += part; n -= part;
        if (c->used == 64) { sha256_block(c, c->block); c->used = 0; }
    }
}
static inline void sha256_final(sha256_ctx *c, uint8_t out[32])
{
    uint64_t bits = c->bytes * 8;
    uint8_t pad = 0x80, zero = 0, length[8];
    sha256_update(c, &pad, 1);
    while (c->used != 56) sha256_update(c, &zero, 1);
    for (int i = 0; i < 8; ++i) length[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_update(c, length, 8);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j) out[4 * i + j] = (uint8_t)(c->h[i] >> (24 - 8 * j));
}
#endif

static inline void sha256_hex(const uint8_t digest[32], char out[65])
{
    for (int i = 0; i < 32; ++i) snprintf(out + 2 * i, 3, "%02x", digest[i]);
}
#endif
