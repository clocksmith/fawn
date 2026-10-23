/* SPDX-License-Identifier: MIT
 * Independent M12 exact/one-short discovery against presealed case bytes.
 * The discoverable quantity is capacity, never semantic expected behavior. */
#define _GNU_SOURCE
#include "environment_m12.h"
#include <stddef.h>

typedef struct {
    const char *name;
    size_t offset;
    unsigned id;
} Pool;

static const Pool pools[] = {
    {"E", offsetof(DoeCompilerTypedCapacities, expressions), 4u},
    {"Q", offsetof(DoeCompilerTypedCapacities, types), 5u},
    {"K", offsetof(DoeCompilerTypedCapacities, constants), 9u},
    {"L", offsetof(DoeCompilerTypedCapacities, constant_edges), 10u},
    {"I", offsetof(DoeCompilerTypedCapacities, ir_instructions), 11u},
    {"O", offsetof(DoeCompilerTypedCapacities, ir_operands), 12u},
    {"D", offsetof(DoeCompilerTypedCapacities, target_declarations), 13u},
    {"V", offsetof(DoeCompilerTypedCapacities, traversal_items), 15u},
    {"W", offsetof(DoeCompilerTypedCapacities, words), 16u}
};

static int attempt(const Pool *pool, size_t limit, unsigned flags,
                   const char *source, size_t length, Bulk *outputs,
                   DoeCompilerStatus *returned)
{
    DoeCompilerTypedCapacities caps = generous;
    memcpy((unsigned char *)&caps + pool->offset, &limit, sizeof(limit));
    Client client;
    CHECK(client_new(&client, &caps) == 0, "pool query and allocation");
    DoeCompilerTypedOutputs selected_outputs = selected(outputs, flags);
    DoeCompilerTypedResult result;
    for (unsigned repeat = 0u; repeat < 2u; ++repeat) {
        bulk_mark(outputs);
        NativeEnvironment before, after;
        CHECK(environment_configure(31u) == 0, "pool adverse host state");
        CHECK(environment_snapshot(&before) == 0, "pool state before");
        errno = 21347;
        DoeCompilerStatus status = invoke(
            &client, source, length, flags,
            (flags & 4u) ? NULL : &selected_outputs, &result);
        int saved_errno = errno;
        CHECK(environment_snapshot(&after) == 0, "pool state after");
        CHECK(saved_errno == 21347 && environment_equal(&before, &after),
              "pool success/error restores fenv MXCSR traps errno");
        CHECK(environment_configure(0u) == 0, "pool harness reset");
        if (repeat) CHECK(status == *returned, "same-workspace reuse");
        else *returned = status;
        if (status != DOE_COMPILER_OK) {
            DoeCompilerStatus expected = pool->id == 16u ?
                DOE_COMPILER_OUTPUT_TOO_SMALL : DOE_COMPILER_INPUT_ERROR;
            CHECK(status == expected, "pool status taxonomy");
            CHECK(failure_result(&result, status, 0) == 0,
                  "pool failure has no exact or published outputs");
            CHECK(result.exhausted_pool == pool->id,
                  "isolated first exhausted pool");
            CHECK(result.minimum_pool_capacity > limit,
                  "pool lower bound exceeds supplied capacity");
            CHECK(bulk_unchanged(outputs), "all user outputs unchanged");
        } else {
            CHECK(result.required_is_exact == 1u,
                  "pool success exact selected counts");
            if (flags & 4u) CHECK(bulk_unchanged(outputs),
                                  "measure never publishes buffers");
        }
        CHECK(guard_ok(&client.workspace) && bulk_guards(outputs),
              "capacity workspace and output redzones");
    }
    printf("POOL %s limit=%zu flags=%u status=%d exhausted=%u minimum=%zu\n",
           pool->name, limit, flags, (int)*returned, result.exhausted_pool,
           result.minimum_pool_capacity);
    client_free(&client);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(setvbuf(stdout, NULL, _IOLBF, 0u) == 0, "line-buffered pool progress");
    CHECK(argc == 3, "usage: capacity_m12 E|Q|K|L|I|O|D|V|W source.wgsl");
    const Pool *pool = NULL;
    for (size_t i = 0u; i < COUNT(pools); ++i) {
        if (strcmp(argv[1], pools[i].name) == 0) pool = &pools[i];
    }
    CHECK(pool != NULL, "known sealed pool");
    CHECK(native_allocator_control() == 0, "allocator positive control");
    NativeEnvironment original;
    CHECK(environment_snapshot(&original) == 0, "initial environment");
    char *source = NULL;
    size_t length = 0u;
    CHECK(load_source(argv[2], &source, &length) == 0, "sealed seed load");
    Client client;
    CHECK(client_new(&client, &generous) == 0, "full-profile baseline");
    DoeCompilerTypedResult result;
    CHECK(invoke(&client, source, length, 7u, NULL, &result) ==
          DOE_COMPILER_OK, "sealed success must fit unchanged profile");
    Bulk outputs;
    CHECK(bulk_new(&outputs, &result.required) == 0, "exact external spans");
    client_free(&client);
    size_t low = 0u, high = 0u;
    memcpy(&high, (const unsigned char *)&generous + pool->offset,
           sizeof(high));
    CHECK(high > 0u && high <= 262144u, "frozen bounded search range");
    DoeCompilerStatus status;
    CHECK(attempt(pool, high, 3u, source, length, &outputs, &status) == 0 &&
          status == DOE_COMPILER_OK, "initial upper bound succeeds");
    CHECK(attempt(pool, 0u, 3u, source, length, &outputs, &status) == 0 &&
          status != DOE_COMPILER_OK, "seed actually charges selected pool");
    while (low + 1u < high) {
        size_t middle = low + (high - low) / 2u;
        CHECK(attempt(pool, middle, 3u, source, length, &outputs, &status) == 0,
              "bounded monotone capacity discovery");
        if (status == DOE_COMPILER_OK) high = middle;
        else low = middle;
    }
    const unsigned modes[] = {1u, 2u, 3u, 5u, 6u, 7u};
    for (size_t i = 0u; i < COUNT(modes); ++i) {
        CHECK(attempt(pool, high, modes[i], source, length,
                      &outputs, &status) == 0 && status == DOE_COMPILER_OK,
              "exact capacity succeeds in every output mode");
        CHECK(attempt(pool, high - 1u, modes[i], source, length,
                      &outputs, &status) == 0 && status != DOE_COMPILER_OK,
              "one-short fails atomically in every output mode");
    }
    bulk_free(&outputs);
    free(source);
    CHECK(atomic_load(&native_allocations) == 0u, "library allocation-free");
    CHECK(fesetenv(&original.raw) == 0, "restore initial environment");
    printf("PASS POOL %s exact=%zu one_short=%zu\n",
           pool->name, high, high - 1u);
    return 0;
}
