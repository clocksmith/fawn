#ifndef DOE_VK_INDIRECT_H
#define DOE_VK_INDIRECT_H
#include "doe_vk_bindings.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { DOE_VK_INDIRECT_SOURCE_CAPACITY = 512 };
typedef struct doe_vk_dispatch_validator doe_vk_dispatch_validator;
typedef struct doe_vk_dispatch_validator_desc {
    /* Compiler-validated SPIR-V from exactly the generated source for these
     * limits, entry main and workgroup size1. Compiler admission remains upstream. */
    const uint32_t *spirv;
    size_t word_count;
    uint32_t max_workgroup_count[3];
} doe_vk_dispatch_validator_desc;
/* Source, not CPU argument validation. Outputs stay unchanged on failure; length
 * excludes the NUL, which capacity must also fit. Limits must be nonzero. */
doe_vk_status doe_vk_dispatch_validator_source(const uint32_t limits[3],
    char *out, size_t capacity, size_t *out_length);
/* Allocation-free admission; limits may be narrower than actual device limits. */
doe_vk_status doe_vk_dispatch_validator_validate(const doe_vk_caps *caps,
    const uint32_t limits[3]);
doe_vk_status doe_vk_dispatch_validator_create(doe_vk_context *context,
    const doe_vk_dispatch_validator_desc *desc, doe_vk_dispatch_validator **out,
    doe_vk_error *error);
doe_vk_status doe_vk_dispatch_validator_retain(doe_vk_dispatch_validator *validator);
void doe_vk_dispatch_validator_release(doe_vk_dispatch_validator *validator);
/* Records copy of exactly12 argument bytes -> GPU dimension guard -> native
 * indirect dispatch through the same command owner. Over-limit dimensions zero
 * all counts; existing zero dimensions remain valid no-ops. The guard never
 * mutates the original argument range. Arguments require native INDIRECT|COPY_SRC usage; public
 * usage admission stays upstream. Offset is 4-byte aligned and range checked.
 * Commands retain scratch, validator pipeline, target pipeline and all resources
 * independently of caller validator refs. Serialized queue reuse is allowed;
 * failed scratch requires a replacement validator, unknown work stays retained.
 * Product-sensitive workgroup/global-invocation-index builtins are not admitted
 * by this axis-only guard and must remain rejected by compiler/native admission. */
doe_vk_status doe_vk_commands_dispatch_indirect(doe_vk_commands *commands,
    doe_vk_dispatch_validator *validator, doe_vk_pipeline *pipeline,
    const doe_vk_binding_resource *bindings, size_t binding_count,
    doe_vk_buffer *arguments, uint64_t offset);
#ifdef __cplusplus
}
#endif
#endif
