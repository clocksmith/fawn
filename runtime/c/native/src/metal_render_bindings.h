/* SPDX-License-Identifier: MIT */
#ifndef DOE_NATIVE_METAL_RENDER_BINDINGS_H
#define DOE_NATIVE_METAL_RENDER_BINDINGS_H
#include "metal_binding_internal.h"
typedef struct native_metal_render_binding_cache native_metal_render_binding_cache;
typedef struct native_metal_render_binding_view {
    doe_mtl_render_bindings vertex, fragment;
} native_metal_render_binding_view;
typedef struct native_metal_render_binding_stats {
    size_t states, buffers, textures, samplers;
} native_metal_render_binding_stats;
native_result native_metal_render_bindings_intern(native_metal_render_binding_cache **,
    const doe_resource_render_packet *);
/* Intern and report the state's stable index, for bindings_view after prepare. */
native_result native_metal_render_bindings_intern_index(native_metal_render_binding_cache **,
    const doe_resource_render_packet *, size_t *index);
const native_metal_render_binding_view *native_metal_render_bindings_view(
    const native_metal_render_binding_cache *, size_t index);
native_failure native_metal_render_bindings_prepare(native_metal_render_binding_cache *, doe_mtl_commands *);
const native_metal_render_binding_view *native_metal_render_bindings_find(
    const native_metal_render_binding_cache *, const doe_resource_render_packet *);
native_metal_render_binding_stats native_metal_render_bindings_stats(const native_metal_render_binding_cache *);
void native_metal_render_bindings_release(native_metal_render_binding_cache *);
#endif
