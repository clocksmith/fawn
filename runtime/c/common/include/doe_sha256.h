/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMMON_SHA256_H
#define DOE_COMMON_SHA256_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#endif
#if (defined(__linux__) || (defined(_WIN32) && defined(__clang__))) && \
    (defined(__x86_64__) || defined(__i386__)) && \
    (defined(__GNUC__) || defined(__clang__))
#if defined(_WIN32)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#include <immintrin.h>
#define DOE_SHA256_X86 1
#endif

static const uint32_t doe_sha256_rounds[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static inline uint32_t doe_sha256_rotate(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static inline void doe_sha256_schedule(const uint8_t chunk[64], uint32_t w[64])
{
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)chunk[4 * i] << 24 | (uint32_t)chunk[4 * i + 1] << 16 | (uint32_t)chunk[4 * i + 2] << 8 | chunk[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = doe_sha256_rotate(w[i - 15], 7) ^ doe_sha256_rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = doe_sha256_rotate(w[i - 2], 17) ^ doe_sha256_rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
}

static inline void doe_sha256_block(uint32_t state[8], const uint8_t chunk[64])
{
    uint32_t w[64];
    doe_sha256_schedule(chunk,w);
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (doe_sha256_rotate(e, 6) ^ doe_sha256_rotate(e, 11) ^ doe_sha256_rotate(e, 25)) + ((e & f) ^ (~e & g)) + doe_sha256_rounds[i] + w[i];
        uint32_t t2 = (doe_sha256_rotate(a, 2) ^ doe_sha256_rotate(a, 13) ^ doe_sha256_rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    const uint32_t rounds[8] = {a, b, c, d, e, f, g, h};
    for (int i = 0; i < 8; i++) state[i] += rounds[i];
}

#if defined(DOE_SHA256_X86)
/* Keep ISA-specific code behind the complete runtime feature check. Neither the
 * package nor unrelated callers require a SHA-capable CPU at load time. SHA-NI
 * represents ABEF/CDGH from high to low lane; public state stays A..H. */
__attribute__((target("sha,ssse3,sse4.1")))
static void doe_sha256_block_x86(uint32_t state[8], const uint8_t chunk[64])
{
    const __m128i endian = _mm_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12);
    __m128i schedule[4];
    for (unsigned i=0;i<4;i++)
        schedule[i] = _mm_shuffle_epi8(
            _mm_loadu_si128((const __m128i *)(const void *)(chunk+i*16)),endian);
    __m128i first = _mm_loadu_si128((const __m128i *)(const void *)state);
    __m128i second = _mm_loadu_si128((const __m128i *)(const void *)(state+4));
    __m128i tmp = _mm_shuffle_epi32(first,0xb1);
    second = _mm_shuffle_epi32(second,0x1b);
    first = _mm_alignr_epi8(tmp,second,8);
    second = _mm_blend_epi16(second,tmp,0xf0);
    const __m128i initial_first=first, initial_second=second;
    for (unsigned i=0;i<16;i++) {
        const unsigned slot=i%4;
        __m128i message = _mm_add_epi32(schedule[slot],
            _mm_loadu_si128((const __m128i *)(const void *)(doe_sha256_rounds+i*4)));
        second = _mm_sha256rnds2_epu32(second,first,message);
        message = _mm_shuffle_epi32(message,0x0e);
        first = _mm_sha256rnds2_epu32(first,second,message);
        if (i<12) {
            __m128i next = _mm_sha256msg1_epu32(schedule[slot],schedule[(slot+1)%4]);
            next = _mm_add_epi32(next,
                _mm_alignr_epi8(schedule[(slot+3)%4],schedule[(slot+2)%4],4));
            schedule[slot] = _mm_sha256msg2_epu32(next,schedule[(slot+3)%4]);
        }
    }
    first = _mm_add_epi32(first,initial_first);
    second = _mm_add_epi32(second,initial_second);
    tmp = _mm_shuffle_epi32(first,0x1b);
    second = _mm_shuffle_epi32(second,0xb1);
    first = _mm_blend_epi16(tmp,second,0xf0);
    second = _mm_alignr_epi8(second,tmp,8);
    _mm_storeu_si128((__m128i *)(void *)state,first);
    _mm_storeu_si128((__m128i *)(void *)(state+4),second);
}

static inline int doe_sha256_x86_features(unsigned basic_ecx, unsigned structured_ebx)
{
    const unsigned required = (1u << 9) | (1u << 19); /* SSSE3 and SSE4.1, leaf 1 ECX. */
    return (basic_ecx & required) == required && (structured_ebx & (1u << 29)) != 0;
}

static inline int doe_sha256_x86_available(void)
{
    /* The compiler's cached CPU dispatcher introduces mutable process globals.
     * Read the bounded architectural leaves directly; no cache/once owner is
     * needed for a one-shot hash, and missing leaves retain the software path. */
#if defined(_WIN32)
    int leaf[4];
    __cpuidex(leaf,0,0);
    if ((unsigned)leaf[0] < 7u) return 0;
    __cpuidex(leaf,1,0);
    const unsigned basic_ecx=(unsigned)leaf[2];
    __cpuidex(leaf,7,0);
    return doe_sha256_x86_features(basic_ecx,(unsigned)leaf[1]);
#else
    unsigned eax, ebx, ecx, edx;
    if (!__get_cpuid(1,&eax,&ebx,&ecx,&edx)) return 0;
    const unsigned basic_ecx=ecx;
    if (!__get_cpuid_count(7,0,&eax,&ebx,&ecx,&edx)) return 0;
    return doe_sha256_x86_features(basic_ecx,ebx);
#endif
}
#endif

typedef void (*doe_sha256_compress)(uint32_t state[8], const uint8_t chunk[64]);

static inline void doe_sha256_bytes(const void *data, size_t size, uint8_t digest[32],
    doe_sha256_compress block)
{
    uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const uint8_t *bytes = data;
    size_t full = size / 64;
    for (size_t i = 0; i < full; i++) block(state, bytes + 64 * i);
    uint8_t tail[128] = {0};
    size_t rest = size - 64 * full;
    if (rest) memcpy(tail, bytes + 64 * full, rest);
    tail[rest] = 0x80;
    size_t tail_size = rest < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)size * 8u;
    for (int i = 0; i < 8; i++) tail[tail_size - 1 - (size_t)i] = (uint8_t)(bits >> (8 * i));
    block(state, tail);
    if (tail_size == 128) block(state, tail + 64);
    for (int i = 0; i < 8; i++) {
        digest[4 * i] = (uint8_t)(state[i] >> 24);
        digest[4 * i + 1] = (uint8_t)(state[i] >> 16);
        digest[4 * i + 2] = (uint8_t)(state[i] >> 8);
        digest[4 * i + 3] = (uint8_t)state[i];
    }
}

static inline void doe_common_sha256(const void *data, size_t size, uint8_t digest[32])
{
#if defined(__APPLE__)
    /* CommonCrypto is part of libSystem. Its one-shot length is 32-bit;
     * preserve the portable size_t path for larger inputs or a refused call. */
    uint8_t accelerated[32];
    if (size <= UINT32_MAX && CC_SHA256(data, (CC_LONG)size, accelerated) != NULL) {
        memcpy(digest, accelerated, sizeof(accelerated));
        return;
    }
#endif
#if defined(DOE_SHA256_X86)
    if (doe_sha256_x86_available()) {
        doe_sha256_bytes(data,size,digest,doe_sha256_block_x86);
        return;
    }
#endif
    doe_sha256_bytes(data,size,digest,doe_sha256_block);
}

static inline void doe_common_sha256_hex(const uint8_t digest[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    out[64] = '\0';
}

#endif
