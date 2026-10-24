/* SPDX-License-Identifier: MIT */
#ifndef DOE_METAL_INDIRECT_H
#define DOE_METAL_INDIRECT_H
#include "doe_metal_texture.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct doe_mtl_dispatch_guard doe_mtl_dispatch_guard;
/* Caller compiles the exact shared doe_dispatch_guard_source for max_groups.
 * The compiler supplies the MSL entry, one scratch binding, sizes slot and
 * resolved (1,1,1) workgroup. This backend never parses shader source or accepts
 * unchecked counts. Inputs are borrowed; failure leaves *out untouched. */
typedef struct doe_mtl_dispatch_guard_desc {
    doe_mtl_pipeline_desc pipeline;
    uint32_t binding_slot;
    doe_mtl_dimensions max_groups;
} doe_mtl_dispatch_guard_desc;
doe_mtl_status doe_mtl_dispatch_guard_create(doe_mtl_context *context,
    const doe_mtl_dispatch_guard_desc *desc, doe_mtl_dispatch_guard **out, doe_mtl_error *error);
void doe_mtl_dispatch_guard_release(doe_mtl_dispatch_guard *guard);

typedef struct doe_mtl_indirect_dispatch {
    doe_mtl_dispatch_guard *guard;
    doe_mtl_pipeline *pipeline;
    const doe_mtl_buffer_binding *buffers;
    size_t buffer_count;
    const doe_mtl_texture_binding *textures;
    size_t texture_count;
    const doe_mtl_sampler_binding *samplers;
    size_t sampler_count;
    doe_mtl_buffer *arguments;
    uint64_t argument_offset;
} doe_mtl_indirect_dispatch;
/* GPU-copy exactly 12 argument bytes, run the bound guard on private scratch,
 * then dispatch from that scratch. No host read or caller-buffer mutation.
 * All PSOs/resources/scratch join the existing command leases; unknown retains
 * them. Any partial recording failure poisons the entire unsubmitted command. */
doe_mtl_status doe_mtl_commands_dispatch_indirect(doe_mtl_commands *commands,
    const doe_mtl_indirect_dispatch *dispatch, doe_mtl_error *error);
#ifdef __cplusplus
}
#endif
#endif
