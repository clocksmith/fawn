/* SPDX-License-Identifier: MIT
 * The trace payload hash chain against rows doe-zig-runtime wrote for the same
 * example streams (examples/p0_resource_lifecycle_commands.json,
 * rmsnorm_tree_commands.json, workgroup_atomic_commands.json; doe-zig-runtime
 * built from db0979828, run on lavapipe 2026-10-25), plus an alias stream
 * [write_buffer, buffer_upload] (doe-zig-runtime from the same build, --quirk-mode
 * off, 2026-10-25). Wrong field order, a
 * missing "none" field or a hashed toggle string changes every value.
 * Semantic vectors: doe-zig-runtime trace-only rows (no --execute) for
 * examples/numeric-stability-decode-greedy.commands.json, and executed rows
 * (lavapipe, --quirk-mode off) for a workgroup_atomic stream carrying every
 * semantic field; both 2026-10-25. */
#include "trace.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct vector {
    const char *label;
    const char *kernel;
    uint64_t hash;
    doe_semantic semantic;
} vector;

static void chain(const vector *rows, size_t count)
{
    uint64_t previous = DOE_TRACE_HASH_SEED;
    for (size_t seq = 0; seq < count; seq++) {
        uint64_t hash = doe_trace_payload_hash(previous, seq, rows[seq].label, rows[seq].label, rows[seq].kernel, NULL,
            &rows[seq].semantic);
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
        {"upload", NULL, UINT64_C(0xe462eee947038461), {0}},
        {"upload", NULL, UINT64_C(0x5c51dbe6f396b1ba), {0}},
        {"upload", NULL, UINT64_C(0xe75c2a7925755652), {0}},
        {"barrier", NULL, UINT64_C(0x4417a25ca808591b), {0}},
        {"upload", NULL, UINT64_C(0xb51cc5ee8f6a8133), {0}},
    };
    chain(lifecycle, sizeof(lifecycle) / sizeof(lifecycle[0]));
    const vector rmsnorm[] = {
        {"buffer_write", NULL, UINT64_C(0x3eb0c370aa46f5f9), {0}},
        {"kernel_dispatch", "rmsnorm.wgsl", UINT64_C(0x3ab94f41b34d8349), {0}},
    };
    chain(rmsnorm, 2);
    const vector atomic[] = {{"kernel_dispatch", "workgroup_atomic.wgsl", UINT64_C(0xd00d567969d44b72), {0}}};
    chain(atomic, 1);
    /* doe-zig-runtime labels alias kinds with the normalized name: its rows for
     * [write_buffer, buffer_upload] hash as buffer_write, upload. */
    const vector aliases[] = {
        {"buffer_write", NULL, UINT64_C(0x3eb0c370aa46f5f9), {0}},
        {"upload", NULL, UINT64_C(0x7ac9433e64554c12), {0}},
    };
    chain(aliases, 2);
    assert(doe_trace_payload_hash(DOE_TRACE_HASH_SEED, 0, "write_buffer", "buffer_write", NULL, NULL, NULL) != aliases[0].hash);
    const vector greedy[] = {
        {"buffer_write", NULL, UINT64_C(0x3eb0c370aa46f5f9), {0}},
        {"buffer_write", NULL, UINT64_C(0x92f65df8db1ad54a), {0}},
        {"buffer_write", NULL, UINT64_C(0xe545cdbfbc0063fa), {0}},
        {"kernel_dispatch", "bench/inference-pipeline/kernels/matmul-gemv.wgsl", UINT64_C(0x066766d878bdd7db),
            {.op_id = "decode.final_logits", .stage = "decode_demo", .phase = "final_logits", .has_token_index = true}},
        {"buffer_write", NULL, UINT64_C(0xf570349e8eb94e1b), {0}},
        {"kernel_dispatch", "bench/inference-pipeline/kernels/sample.wgsl", UINT64_C(0xf79bde625797c784),
            {.op_id = "decode.sample_token", .stage = "decode_demo", .phase = "sample_token", .has_token_index = true}},
    };
    chain(greedy, sizeof(greedy) / sizeof(greedy[0]));
    /* Layer index, plan hash (with a quote) and absent stage and phase. */
    const vector semantic[] = {
        {"buffer_write", NULL, UINT64_C(0x3eb0c370aa46f5f9), {0}},
        {"kernel_dispatch", "workgroup_atomic.wgsl", UINT64_C(0x4124865a91c3cc65),
            {.op_id = "test.atomic_accumulate", .stage = "c_parity", .phase = "accumulate", .has_token_index = true,
                .token_index = 3, .has_layer_index = true, .layer_index = 1, .execution_plan_hash = "plan-\"q\"-1"}},
        {"kernel_dispatch", "workgroup_atomic.wgsl", UINT64_C(0x39032e82e4daa181),
            {.op_id = "test.atomic_again", .stage = "c_parity", .phase = "again", .has_token_index = true}},
        {"kernel_dispatch", "workgroup_atomic.wgsl", UINT64_C(0xf48294d320b4db17), {.op_id = "test.only_op"}},
        {"kernel_dispatch", "workgroup_atomic.wgsl", UINT64_C(0xeaf883dd6c6b0c40), {0}},
        {"kernel_dispatch", "workgroup_atomic.wgsl", UINT64_C(0x7c3076098b52e104), {.op_id = "test.only_op"}},
    };
    chain(semantic, sizeof(semantic) / sizeof(semantic[0]));
    /* A present zero token index differs from an absent one. */
    const doe_semantic zero = {.op_id = "op", .has_token_index = true}, absent = {.op_id = "op"};
    assert(doe_trace_payload_hash(DOE_TRACE_HASH_SEED, 0, "kernel_dispatch", "kernel_dispatch", "k", NULL, &zero) !=
        doe_trace_payload_hash(DOE_TRACE_HASH_SEED, 0, "kernel_dispatch", "kernel_dispatch", "k", NULL, &absent));
    /* An absent kernel differs from an empty one. */
    assert(doe_trace_payload_hash(DOE_TRACE_HASH_SEED, 0, "kernel_dispatch", "kernel_dispatch", NULL, NULL, NULL) !=
        doe_trace_payload_hash(DOE_TRACE_HASH_SEED, 0, "kernel_dispatch", "kernel_dispatch", "", NULL, NULL));
    puts("runtime-cli trace: hash chain equals doe-zig-runtime rows for three example streams, alias labels and two "
        "semantic streams (host only)");
    return 0;
}
