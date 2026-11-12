/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

/* Copies precede guards; normalize/scan dispatches share one concurrent
 * compute encoder with a buffer barrier wherever a dispatch requires every
 * earlier dispatch's writes. A serial encoder would also wait between the
 * independent dispatches of one phase. */
doe_mtl_status doe_mtl_objc_guard_batch(void *userdata, void *command,
    const doe_mtl_native_guard_copy *copies, size_t copy_count,
    const doe_mtl_native_guard_dispatch *dispatches, size_t dispatch_count, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    void *pool = doe_mtl_objc_pool(r), *encoder = NULL;
    if (!pool) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    if (copy_count) {
        encoder = doe_mtl_objc_object(r, command, r->blit_encoder);
        if (!encoder) goto blit_failure;
        for (size_t i = 0; i < copy_count; ++i) r->copy_call(encoder, r->copy_buffer,
            copies[i].source, copies[i].source_offset, copies[i].destination,
            copies[i].destination_offset, copies[i].size);
        doe_mtl_objc_void(r, encoder, r->end_encoding);
    }
    if (dispatch_count) {
        encoder = r->object_size_call(command, r->compute_encoder_with_type, 1); /* MTLDispatchTypeConcurrent. */
        if (!encoder) goto compute_failure;
        if (!r->responds_call(encoder, r->responds, r->buffer_barrier)) {
            doe_mtl_objc_void(r, encoder, r->end_encoding);
            doe_mtl_objc_drain(r, pool);
            return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "memoryBarrierWithScope:", error);
        }
        for (size_t i = 0; i < dispatch_count; ++i) {
            const doe_mtl_native_guard_dispatch *step = &dispatches[i];
            uint32_t sizes[DOE_MTL_BUFFER_SLOTS] = {0};
            if (step->barrier) r->set_size_call(encoder, r->buffer_barrier, 1); /* MTLBarrierScopeBuffers. */
            r->set_object_call(encoder, r->set_pipeline, step->pipeline);
            for (uint32_t j = 0; j < step->binding_count; ++j) {
                const doe_mtl_native_binding *binding = &step->bindings[j];
                r->bind_buffer_call(encoder, r->set_buffer, binding->buffer, binding->offset, step->slots[j]);
                sizes[step->slots[j]] = binding->size;
            }
            if (step->sizes_slot != UINT32_MAX) r->bind_bytes_call(encoder, r->set_bytes,
                sizes, ((size_t)step->sizes_slot + 1) * sizeof(uint32_t), step->sizes_slot);
            r->dispatch_call(encoder, r->dispatch,
                (doe_mtl_size){step->groups.x,step->groups.y,step->groups.z},
                (doe_mtl_size){step->workgroup.x,step->workgroup.y,step->workgroup.z});
        }
        doe_mtl_objc_void(r, encoder, r->end_encoding);
    }
    doe_mtl_objc_drain(r, pool);
    return doe_mtl_fail(DOE_MTL_OK, "draw guard batch", error);
blit_failure:
    doe_mtl_objc_drain(r, pool);
    return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "blitCommandEncoder", error);
compute_failure:
    doe_mtl_objc_drain(r, pool);
    return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "computeCommandEncoder", error);
}
