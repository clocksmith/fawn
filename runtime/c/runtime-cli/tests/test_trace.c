/* SPDX-License-Identifier: MIT
 * The trace payload hash chain against rows doe-zig-runtime wrote for the same
 * example streams (examples/p0_resource_lifecycle_commands.json,
 * rmsnorm_tree_commands.json, workgroup_atomic_commands.json; doe-zig-runtime
 * built from db0979828, run on lavapipe 2026-10-25). Wrong field order, a
 * missing "none" field or a hashed toggle string changes every value. */
#include "trace.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct vector {
    const char *label;
    const char *kernel;
    uint64_t hash;
} vector;

static void chain(const vector *rows, size_t count)
{
    uint64_t previous = DOE_TRACE_HASH_SEED;
    for (size_t seq = 0; seq < count; seq++) {
        uint64_t hash = doe_trace_payload_hash(previous, seq, rows[seq].label, rows[seq].label, rows[seq].kernel);
        if (hash != rows[seq].hash) {
            fprintf(stderr, "row %zu (%s): 0x%llx, expected 0x%llx\n", seq, rows[seq].label,
                (unsigned long long)hash, (unsigned long long)rows[seq].hash);
        }
        assert(hash == rows[seq].hash);
        previous = hash;
    }
}

int main(void)
{
    const vector lifecycle[] = {
        {"upload", NULL, UINT64_C(0xe462eee947038461)},
        {"upload", NULL, UINT64_C(0x5c51dbe6f396b1ba)},
        {"upload", NULL, UINT64_C(0xe75c2a7925755652)},
        {"barrier", NULL, UINT64_C(0x4417a25ca808591b)},
        {"upload", NULL, UINT64_C(0xb51cc5ee8f6a8133)},
    };
    chain(lifecycle, sizeof(lifecycle) / sizeof(lifecycle[0]));
    const vector rmsnorm[] = {
        {"buffer_write", NULL, UINT64_C(0x3eb0c370aa46f5f9)},
        {"kernel_dispatch", "rmsnorm.wgsl", UINT64_C(0x3ab94f41b34d8349)},
    };
    chain(rmsnorm, 2);
    const vector atomic[] = {{"kernel_dispatch", "workgroup_atomic.wgsl", UINT64_C(0xd00d567969d44b72)}};
    chain(atomic, 1);
    /* An alias label hashes as written while the kind tag is normalized. */
    assert(doe_trace_payload_hash(DOE_TRACE_HASH_SEED, 0, "write_buffer", "buffer_write", NULL) !=
        doe_trace_payload_hash(DOE_TRACE_HASH_SEED, 0, "buffer_write", "buffer_write", NULL));
    /* An absent kernel differs from an empty one. */
    assert(doe_trace_payload_hash(DOE_TRACE_HASH_SEED, 0, "kernel_dispatch", "kernel_dispatch", NULL) !=
        doe_trace_payload_hash(DOE_TRACE_HASH_SEED, 0, "kernel_dispatch", "kernel_dispatch", ""));
    puts("runtime-cli trace: hash chain equals doe-zig-runtime rows for three example streams (host only)");
    return 0;
}
