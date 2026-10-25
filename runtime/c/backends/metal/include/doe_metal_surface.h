/* SPDX-License-Identifier: MIT */
#ifndef DOE_METAL_SURFACE_H
#define DOE_METAL_SURFACE_H
#include "doe_metal_texture.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct doe_mtl_surface doe_mtl_surface;
typedef struct doe_mtl_surface_desc {
    uint32_t width, height, usage;
    doe_mtl_format format;
    bool synchronized, opaque;
} doe_mtl_surface_desc;
/* The caller supplies a real CAMetalLayer. Configuration retains it; each
 * acquired texture retains its configuration until that texture is released. */
bool doe_mtl_surface_supported(doe_mtl_context *context, void *layer);
doe_mtl_status doe_mtl_surface_create(doe_mtl_context *context, void *layer,
    const doe_mtl_surface_desc *desc, doe_mtl_surface **out, doe_mtl_error *error);
void doe_mtl_surface_release(doe_mtl_surface *surface);
doe_mtl_status doe_mtl_surface_acquire(doe_mtl_surface *surface,
    doe_mtl_texture **out, doe_mtl_error *error);
/* Registers real drawable presentation on this existing command before commit.
 * The native scheduled handler owns presentation after registration. We retain
 * the drawable until GPU completion; unknown GPU completion retains ordinary
 * command/texture obligations. No onscreen timestamp is implied by completion. */
doe_mtl_status doe_mtl_commands_present(doe_mtl_commands *commands,
    doe_mtl_texture *texture, doe_mtl_error *error);
#ifdef __cplusplus
}
#endif
#endif
