/* SPDX-License-Identifier: MIT
 * CPU reference for the snapshot-branch timing executor
 * (bench/executors/snapshot-branch-native.c). No WebGPU: the expected base and
 * branch states are computed on host words from the workload definition in
 * runtime/c/native/tests/snapshot_ledger.c and snapshot_scratch.h, then hashed
 * in the executor's byte order (tracked buffers in order, then each texture's
 * tightly packed RGBA8 rows).
 *
 *   w        W0/W1 2 x 32 MiB over R0..R3 4 x 48 MiB, 8 steps x 128 of 1,024
 *            64 KiB pages per branch.
 *   scratch  48, 48, 32, 16, 16 and 4 MiB read_write buffers, a 1280x1024
 *            RGBA8 storage atlas with a 64x64 update and a 956x655 RGBA8
 *            target redrawn per branch.
 *
 * Every branch starts from the base. Usage: snapshot-branch-reference
 * [branches] [size divisor], default 32 and 1. Prints one JSON object. */
#include "sha256_small.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>

enum { STEPS = 8, MAX_PAGES = 1024, PAGE_WORDS = 16384, SCRATCH_BUFFERS = 6, ATLAS_W = 1280, ATLAS_H = 1024,
    VIEW_W = 956, VIEW_H = 655, SCRATCH_INVOCATIONS = 400 * 64 };
static const uint64_t BASE_SEED = 20261123u;
static const uint64_t scratch_mib[SCRATCH_BUFFERS] = {48, 48, 32, 16, 16, 4};
/* Size divisor, as in the executor: 1024/d pages, R = 1.5 x W, scratch 1/d. */
static uint32_t divisor = 1, PAGES, SELECTED, W_WORDS, R_WORDS;
static uint64_t scratch_bytes(size_t i) { return (scratch_mib[i] << 20) / divisor; }

static uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}
static void splitmix_words(uint32_t *words, size_t count, uint64_t seed)
{
    for (size_t i = 0; i < count; ++i) words[i] = (uint32_t)splitmix64(&seed);
}
static uint64_t branch_seed(uint32_t branch)
{
    uint64_t state = BASE_SEED + 1000u + branch;
    return splitmix64(&state);
}
static void step_pages(uint32_t branch, uint32_t step, uint32_t *out)
{
    uint32_t order[MAX_PAGES];
    for (uint32_t i = 0; i < PAGES; ++i) order[i] = i;
    uint64_t state = branch_seed(branch) + step;
    for (uint32_t k = 0; k < SELECTED; ++k) {
        uint32_t j = k + (uint32_t)(splitmix64(&state) % (PAGES - k));
        uint32_t swap = order[k];
        order[k] = order[j];
        order[j] = swap;
        out[k] = order[k];
    }
}
static uint32_t rotl7(uint32_t x) { return (x << 7) | (x >> 25); }

static void *take(size_t bytes)
{
    void *p = malloc(bytes);
    assert(p);
    return p;
}
static void reference_w(uint32_t branches)
{
    uint32_t *r[4], *base = take((size_t)2 * W_WORDS * 4), *w = take((size_t)2 * W_WORDS * 4);
    for (uint32_t k = 0; k < 4; ++k) {
        r[k] = take((size_t)R_WORDS * 4);
        splitmix_words(r[k], R_WORDS, BASE_SEED + 1 + k);
    }
    for (uint32_t k = 0; k < 2; ++k) splitmix_words(base + (size_t)k * W_WORDS, W_WORDS, BASE_SEED + 5 + k);
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, base, (size_t)2 * W_WORDS * 4);
    printf("\"w\":{\"base\":\"");
    uint8_t digest[32];
    char hex[65];
    sha256_final(&c, digest);
    sha256_hex(digest, hex);
    printf("%s\",\"branches\":[", hex);
    for (uint32_t b = 0; b < branches; ++b) {
        memcpy(w, base, (size_t)2 * W_WORDS * 4);
        const uint32_t seed = (uint32_t)branch_seed(b);
        uint32_t pages[MAX_PAGES / 8];
        for (uint32_t t = 0; t < STEPS; ++t) {
            step_pages(b, t, pages);
            for (uint32_t s = 0; s < SELECTED; ++s)
                for (uint32_t k = 0; k < PAGE_WORDS; ++k) {
                    uint32_t i = pages[s] * PAGE_WORDS + k;
                    uint32_t j = (i * 2654435761u + t) % R_WORDS;
                    w[i] = rotl7(w[i] ^ r[(i >> 12) & 3][j]) * 2147483659u + seed;
                }
        }
        sha256_init(&c);
        sha256_update(&c, w, (size_t)2 * W_WORDS * 4);
        sha256_final(&c, digest);
        sha256_hex(digest, hex);
        printf("\"%s\"%s", hex, b + 1 < branches ? "," : "");
    }
    printf("]}");
    for (uint32_t k = 0; k < 4; ++k) free(r[k]);
    free(base);
    free(w);
}

/* WGSL rgba8unorm conversion of an f32 in [0, 1]: round to nearest. */
static uint8_t unorm8(float v)
{
    float scaled = v * 255.0f;
    return (uint8_t)lrintf(scaled);
}

static void scratch_hash(uint32_t *const *buffers, const uint8_t *atlas, const uint8_t *view, char hex[65])
{
    sha256_ctx c;
    uint8_t digest[32];
    sha256_init(&c);
    for (size_t i = 0; i < SCRATCH_BUFFERS; ++i) sha256_update(&c, buffers[i], (size_t)scratch_bytes(i));
    sha256_update(&c, atlas, (size_t)ATLAS_W * ATLAS_H * 4);
    sha256_update(&c, view, (size_t)VIEW_W * VIEW_H * 4);
    sha256_final(&c, digest);
    sha256_hex(digest, hex);
}

static void reference_scratch(uint32_t branches)
{
    uint32_t *base[SCRATCH_BUFFERS], *now[SCRATCH_BUFFERS];
    for (size_t i = 0; i < SCRATCH_BUFFERS; ++i) {
        const size_t words = (size_t)scratch_bytes(i) / 4;
        base[i] = take(words * 4);
        now[i] = take(words * 4);
        splitmix_words(base[i], words, BASE_SEED + 100 + i);
        if (i == 4) for (size_t w = 0; w < words; ++w) base[i][w] &= 0x7fffffffu;
    }
    uint8_t *atlas_base = calloc((size_t)ATLAS_W * ATLAS_H, 4), *view_base = calloc((size_t)VIEW_W * VIEW_H, 4);
    uint8_t *atlas = take((size_t)ATLAS_W * ATLAS_H * 4), *view = take((size_t)VIEW_W * VIEW_H * 4);
    assert(atlas_base && view_base);
    char hex[65];
    scratch_hash(base, atlas_base, view_base, hex);
    printf("\"scratch\":{\"base\":\"%s\",\"branches\":[", hex);
    for (uint32_t b = 0; b < branches; ++b) {
        const uint32_t fx = b * 2654435761u + 1u, fy = (b * 97u) % (ATLAS_W - 64), fz = (b * 61u) % (ATLAS_H - 64);
        for (size_t i = 0; i < SCRATCH_BUFFERS; ++i) memcpy(now[i], base[i], (size_t)scratch_bytes(i));
        memcpy(atlas, atlas_base, (size_t)ATLAS_W * ATLAS_H * 4);
        for (uint32_t i = 0; i < SCRATCH_INVOCATIONS; ++i) {
            if (i < 10240u) now[0][i] = i ^ fx;
            if (i < 6144u) now[1][i] = i + fx;
            if (i < 25600u) now[2][i] = i * fx + 1u;
            if (i < 10240u) now[3][262144u + i] = fx;
            if (now[4][i] == 0xffffffffu) now[5][i] = now[4][i];
            if (i < 4096u) {
                uint8_t *texel = atlas + ((size_t)(fz + i / 64u) * ATLAS_W + fy + i % 64u) * 4;
                texel[0] = unorm8((float)(fx & 255u) / 255.0f);
                texel[1] = unorm8(0.5f);
                texel[2] = unorm8(0.25f);
                texel[3] = 255;
            }
        }
        /* The full-screen triangle covers every texel; fragment position is the texel center. */
        for (uint32_t y = 0; y < VIEW_H; ++y)
            for (uint32_t x = 0; x < VIEW_W; ++x) {
                uint8_t *texel = view + ((size_t)y * VIEW_W + x) * 4;
                texel[0] = unorm8(((float)x + 0.5f) / 956.0f);
                texel[1] = unorm8(((float)y + 0.5f) / 655.0f);
                texel[2] = unorm8((float)(fx & 255u) / 255.0f);
                texel[3] = 255;
            }
        scratch_hash(now, atlas, view, hex);
        printf("\"%s\"%s", hex, b + 1 < branches ? "," : "");
    }
    printf("]}");
    for (size_t i = 0; i < SCRATCH_BUFFERS; ++i) { free(base[i]); free(now[i]); }
    free(atlas_base); free(view_base); free(atlas); free(view);
}

int main(int argc, char **argv)
{
    uint32_t branches = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 32;
    divisor = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 1;
    assert(branches >= 1 && branches <= 64);
    assert(divisor == 1 || divisor == 2 || divisor == 4 || divisor == 8);
    PAGES = MAX_PAGES / divisor;
    SELECTED = PAGES / 8;
    W_WORDS = PAGES / 2 * PAGE_WORDS;
    R_WORDS = W_WORDS / 2 * 3;
    printf("{\"oracle\":\"snapshot_branch_reference_v1\",\"branches\":%u,\"size_divisor\":%u,", branches, divisor);
    reference_w(branches);
    printf(",");
    reference_scratch(branches);
    printf("}\n");
    return 0;
}
