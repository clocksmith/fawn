/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

static void *stencil_descriptor(doe_mtl_objc *r, doe_mtl_stencil_face face,
    uint32_t read_mask, uint32_t write_mask) {
    doe_mtl_objc_graphics *g = &r->graphics;
    void *descriptor = doe_mtl_objc_object(r, g->stencil_class, r->new_object);
    if (descriptor == NULL) return NULL;
    r->set_size_call(descriptor, g->set_stencil_compare, face.compare);
    r->set_size_call(descriptor, g->set_stencil_fail, face.fail);
    r->set_size_call(descriptor, g->set_depth_fail, face.depth_fail);
    r->set_size_call(descriptor, g->set_stencil_pass, face.pass);
    g->set_u32(descriptor, g->set_read_mask, read_mask);
    g->set_u32(descriptor, g->set_stencil_write_mask, write_mask);
    return descriptor;
}
void *doe_mtl_objc_depth_state(doe_mtl_objc *r, void *device, const doe_mtl_depth_stencil *desc) {
    doe_mtl_objc_graphics *g = &r->graphics;
    void *descriptor = doe_mtl_objc_object(r, g->depth_class, r->new_object);
    void *front = NULL, *back = NULL, *state = NULL;
    if (descriptor == NULL) return NULL;
    r->set_size_call(descriptor, g->set_depth_compare, desc->depth_compare);
    g->set_bool(descriptor, g->set_depth_write, (doe_objc_bool)desc->depth_write);
    if (desc->stencil_format != 0) {
        front = stencil_descriptor(r, desc->front, desc->stencil_read_mask, desc->stencil_write_mask);
        if (front == NULL) goto cleanup;
        back = stencil_descriptor(r, desc->back, desc->stencil_read_mask, desc->stencil_write_mask);
        if (back == NULL) goto cleanup;
        r->set_object_call(descriptor, g->set_front_stencil, front);
        r->set_object_call(descriptor, g->set_back_stencil, back);
    }
    state = r->named_call(device, g->new_depth, descriptor);
cleanup:
    if (back != NULL) doe_mtl_objc_void(r, back, r->release);
    if (front != NULL) doe_mtl_objc_void(r, front, r->release);
    doe_mtl_objc_void(r, descriptor, r->release);
    return state;
}
