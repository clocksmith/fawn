/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_md5.h"

#include <string.h>

typedef struct DxilMd5Ctx {
    uint32_t state[4];
    uint64_t bit_len;
    uint8_t block[64];
    size_t block_len;
} DxilMd5Ctx;

static uint32_t rotl32(uint32_t value, unsigned count) {
    return (value << count) | (value >> (32u - count));
}

static const uint32_t K_TABLE[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

static const unsigned SHIFT_TABLE[64] = {
    7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u,
    5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u, 5u, 9u, 14u, 20u,
    4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u,
    6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u
};

static void process_block(DxilMd5Ctx *ctx, const uint8_t block[64]) {
    uint32_t words[16];
    size_t i = 0;
    for (i = 0; i < 16; i++) {
        words[i] = (uint32_t)block[i * 4]
            | ((uint32_t)block[i * 4 + 1] << 8)
            | ((uint32_t)block[i * 4 + 2] << 16)
            | ((uint32_t)block[i * 4 + 3] << 24);
    }
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    unsigned round = 0;
    for (round = 0; round < 64; round++) {
        uint32_t f = 0;
        unsigned g = 0;
        if (round < 16) {
            f = (b & c) | (~b & d);
            g = round;
        } else if (round < 32) {
            f = (d & b) | (~d & c);
            g = (5u * round + 1u) % 16u;
        } else if (round < 48) {
            f = b ^ c ^ d;
            g = (3u * round + 5u) % 16u;
        } else {
            f = c ^ (b | ~d);
            g = (7u * round) % 16u;
        }
        const uint32_t temp = d;
        d = c;
        c = b;
        b = b + rotl32(a + f + K_TABLE[round] + words[g], SHIFT_TABLE[round]);
        a = temp;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

static void update(DxilMd5Ctx *ctx, const uint8_t *data, size_t len) {
    size_t pos = 0;
    ctx->bit_len += (uint64_t)len * 8u;
    while (pos < len) {
        const size_t take = 64 - ctx->block_len < len - pos
            ? 64 - ctx->block_len : len - pos;
        memcpy(ctx->block + ctx->block_len, data + pos, take);
        ctx->block_len += take;
        pos += take;
        if (ctx->block_len == 64) {
            process_block(ctx, ctx->block);
            ctx->block_len = 0;
        }
    }
}

static void finish(DxilMd5Ctx *ctx, uint8_t out[16]) {
    const uint64_t bit_len = ctx->bit_len;
    const uint8_t pad_start = 0x80u;
    update(ctx, &pad_start, 1);
    /* update() above also advanced bit_len; restore the true length for
     * the trailer. The padding zeros are fed without counting them. */
    const uint8_t zero = 0u;
    while (ctx->block_len != 56) {
        /* Feed zeros directly into the block, bypassing update()'s
         * length accounting, which must stay at the message length. */
        ctx->block[ctx->block_len] = zero;
        ctx->block_len++;
        if (ctx->block_len == 64) {
            process_block(ctx, ctx->block);
            ctx->block_len = 0;
        }
    }
    size_t i = 0;
    for (i = 0; i < 8; i++) {
        ctx->block[56 + i] = (uint8_t)(bit_len >> (i * 8));
    }
    process_block(ctx, ctx->block);
    for (i = 0; i < 4; i++) {
        out[i * 4] = (uint8_t)ctx->state[i];
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i] >> 24);
    }
}

void dxil_md5(const uint8_t *data, size_t len, uint8_t out[16]) {
    DxilMd5Ctx ctx;
    ctx.state[0] = 0x67452301u;
    ctx.state[1] = 0xefcdab89u;
    ctx.state[2] = 0x98badcfeu;
    ctx.state[3] = 0x10325476u;
    ctx.bit_len = 0;
    ctx.block_len = 0;
    /* A NULL data pointer is only valid with a zero length. */
    if (len > 0) {
        update(&ctx, data, len);
    }
    finish(&ctx, out);
}
