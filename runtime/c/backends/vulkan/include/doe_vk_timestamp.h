/* SPDX-License-Identifier: MIT */
#ifndef DOE_VK_TIMESTAMP_H
#define DOE_VK_TIMESTAMP_H
#include "doe_vk_queries.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { DOE_VK_TIMESTAMP_SOURCE_CAPACITY = 4096 };
typedef struct doe_vk_timestamp_normalizer doe_vk_timestamp_normalizer;
/* Produces the retained exact f32-period, base-2^16 WGSL kernel, not timestamp
 * values. Compile with the existing compiler owner. All outputs remain unchanged
 * on failure; length excludes the trailing NUL, which capacity must also fit.
 * The conversion is floor((ticks & counter_mask) * period) modulo 2^64. */
doe_vk_status doe_vk_timestamp_shader_source(float period_ns, uint32_t valid_bits,
    uint32_t workgroup_size, char *out, size_t capacity, size_t *out_length);

typedef struct doe_vk_timestamp_normalizer_desc {
    /* Compiler-validated SPIR-V from exactly the generated source using this
     * context's timestamp period/valid bits and the following workgroup size.
     * Reflection/capability admission remains with the compiler consumer. */
    const uint32_t *spirv;
    size_t word_count;
    uint32_t workgroup_size;
    uint32_t capacity; /* Maximum query count per resolve; nonzero. */
    float period_ns; /* Calibration used to generate source; must match context. */
    uint32_t valid_bits;
} doe_vk_timestamp_normalizer_desc;
/* Allocation-free device-feature admission. No compiler, native calls or
 * output mutation; constructor reuses exactly these capacity/calibration rules. */
doe_vk_status doe_vk_timestamp_normalizer_validate(const doe_vk_caps *caps,
    uint32_t capacity, uint32_t workgroup_size);
doe_vk_status doe_vk_timestamp_normalizer_create(doe_vk_context *context,
    const doe_vk_timestamp_normalizer_desc *descriptor,
    doe_vk_timestamp_normalizer **out, doe_vk_error *error);
doe_vk_status doe_vk_timestamp_normalizer_retain(doe_vk_timestamp_normalizer *normalizer);
void doe_vk_timestamp_normalizer_release(doe_vk_timestamp_normalizer *normalizer);
/* Records raw resolve -> scratch conversion -> destination copy in this command
 * stream. Later copies observe nanoseconds, not ticks. The existing command
 * owner retains scratch and pipeline independently of caller normalizer refs.
 * Reusable on this serialized context's one queue; failed scratch must be replaced
 * by creating another normalizer. No CPU timestamp readback or patching occurs.
 * Packed native destination offset is 8-byte aligned; public alignment is upstream.
 * Count zero is a validated no-op, preserving the raw resolve's pool checks. */
doe_vk_status doe_vk_commands_resolve_queries_ns(doe_vk_commands *commands,
    doe_vk_query_pool *pool, uint32_t first, uint32_t count,
    doe_vk_timestamp_normalizer *normalizer, doe_vk_buffer *destination, uint64_t offset);
#ifdef __cplusplus
}
#endif
#endif
