/* SPDX-License-Identifier: MIT
 * Exercise the real loader/hash boundary without opening a GPU. */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "../../common/include/doe_sha256.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

static size_t hashed_bytes;
static void measured_sha256(const void *bytes, size_t size, uint8_t out[32])
{
    hashed_bytes += size;
    doe_common_sha256(bytes, size, out);
}
#define doe_common_sha256 measured_sha256
#include "../src/shader_cache_identity.c"
#undef doe_common_sha256

static uint64_t now_ns(void)
{
    struct timespec value;
    assert(clock_gettime(CLOCK_MONOTONIC, &value) == 0);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

int main(void)
{
    uintptr_t anchor = 0;
    bool (*function)(uint8_t *) = native_shader_cache_identity;
    _Static_assert(sizeof(anchor) == sizeof(function), "function address size");
    memcpy(&anchor, &function, sizeof(anchor));
    image_hash original = {0}, cached = {0}, uncached = {0};
    uint64_t start = now_ns();
    assert(loaded_image(anchor, NULL, 0, &original));
    uint64_t full_ns = now_ns() - start;
    const size_t original_bytes = hashed_bytes;
    assert(original.spans && original_bytes);
    start = now_ns();
    assert(loaded_image(anchor, &original, 1, &cached));
    uint64_t reuse_ns = now_ns() - start;
    assert(hashed_bytes == original_bytes);
    assert(!memcmp(&original, &cached, sizeof(original)));
    image_hash wrong_owner = original;
    wrong_owner.owner ^= UINTPTR_MAX;
    assert(loaded_image(anchor, &wrong_owner, 1, &uncached));
    assert(hashed_bytes == original_bytes * 2);
    assert(!memcmp(original.digest, uncached.digest, sizeof(original.digest)));
    image_hash incomplete = {.owner=original.owner};
    uncached = (image_hash){0};
    assert(loaded_image(anchor, &incomplete, 1, &uncached));
    assert(hashed_bytes == original_bytes * 3);
    assert(!memcmp(original.digest, uncached.digest, sizeof(original.digest)));
    uint8_t public_identity[32];
    assert(native_shader_cache_identity(public_identity));
    printf("shader image reuse: exact immutable digest, wrong/incomplete owners rehashed; "
        "one image %zu bytes, full=%llu ns, reuse=%llu ns (CPU only)\n",
        original_bytes, (unsigned long long)full_ns, (unsigned long long)reuse_ns);
    return 0;
}
