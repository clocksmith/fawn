/* SPDX-License-Identifier: MIT */
#include "texture_internal.h"

doe_mtl_status doe_mtl_texture_preflight(const doe_mtl_commands *commands,
    const doe_mtl_texture *texture, uint32_t usage) {
    if (texture == NULL) return DOE_MTL_INVALID_ARGUMENT;
    if (texture->context != commands->context) return DOE_MTL_DEVICE_MISMATCH;
    if (texture->presentation_registered) return DOE_MTL_INVALID_STATE;
    if (texture->poisoned) return DOE_MTL_EXECUTION_FAILED;
    if (texture->owner != NULL && texture->owner != commands) return DOE_MTL_BUSY;
    if ((texture->desc.usage & usage) != usage) return DOE_MTL_INVALID_ARGUMENT;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_use_texture(doe_mtl_commands *commands,
    doe_mtl_texture *texture, doe_mtl_texture_use **out) {
    doe_mtl_texture_use *use;
    doe_mtl_status status;
    for (use = commands->textures; use != NULL; use = use->next) {
        if (use->texture == texture) {
            *out = use;
            return DOE_MTL_OK;
        }
    }
    use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_MTL_OUT_OF_MEMORY;
    use->initialized = malloc(texture->subresource_count * sizeof(uint8_t));
    if (use->initialized == NULL) {
        free(use);
        return DOE_MTL_OUT_OF_MEMORY;
    }
    status = doe_mtl_texture_retain(texture);
    if (status != DOE_MTL_OK) {
        free(use->initialized);
        free(use);
        return status;
    }
    memcpy(use->initialized, texture->initialized, texture->subresource_count * sizeof(uint8_t));
    use->texture = texture;
    use->next = commands->textures;
    commands->textures = use;
    texture->owner = commands;
    *out = use;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_image_bindings_preflight(doe_mtl_commands *commands,
    const doe_mtl_texture_binding *textures, size_t texture_count,
    const doe_mtl_sampler_binding *samplers, size_t sampler_count,
    doe_mtl_native_resources *native) {
    size_t i;
    if (texture_count > DOE_MTL_TEXTURE_SLOTS || sampler_count > DOE_MTL_SAMPLER_SLOTS ||
        (texture_count != 0 && textures == NULL) || (sampler_count != 0 && samplers == NULL))
        return DOE_MTL_INVALID_ARGUMENT;
    for (i = 0; i < texture_count; ++i) {
        const doe_mtl_texture_binding *binding = &textures[i];
        doe_mtl_status status;
        if (binding->view == NULL || binding->slot >= DOE_MTL_TEXTURE_SLOTS ||
            native->textures[binding->slot] != NULL) return DOE_MTL_INVALID_ARGUMENT;
        status = doe_mtl_texture_preflight(commands, binding->view->texture,
            binding->storage ? DOE_MTL_TEXTURE_STORAGE : DOE_MTL_TEXTURE_SAMPLED);
        if (status != DOE_MTL_OK) return status;
        if (binding->storage) {
            doe_mtl_format_info format;
            if (binding->view->desc.mip_count != 1) return DOE_MTL_INVALID_ARGUMENT;
            status = doe_mtl_texture_format_info(binding->view->desc.format, &format);
            if (status != DOE_MTL_OK || format.srgb || format.compressed || format.depth || format.stencil) return DOE_MTL_UNSUPPORTED;
        }
        /* Combined-format shader views expose depth; X24/X32 views expose stencil. */
        if (!doe_mtl_texture_aspect_initialized(commands, binding->view,
            (doe_mtl_format_aspects(binding->view->desc.format) & DOE_MTL_ASPECT_DEPTH) != 0 ?
                DOE_MTL_ASPECT_DEPTH : doe_mtl_format_aspects(binding->view->desc.format)))
            return DOE_MTL_UNINITIALIZED;
        native->textures[binding->slot] = binding->view->native;
        if (binding->slot >= native->texture_count) native->texture_count = binding->slot + 1;
    }
    for (i = 0; i < sampler_count; ++i) {
        const doe_mtl_sampler_binding *binding = &samplers[i];
        if (binding->sampler == NULL || binding->slot >= DOE_MTL_SAMPLER_SLOTS ||
            native->samplers[binding->slot] != NULL) return DOE_MTL_INVALID_ARGUMENT;
        if (binding->sampler->context != commands->context) return DOE_MTL_DEVICE_MISMATCH;
        native->samplers[binding->slot] = binding->sampler->native;
        if (binding->slot >= native->sampler_count) native->sampler_count = binding->slot + 1;
    }
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_use_view(doe_mtl_commands *commands, doe_mtl_texture_view *view) {
    doe_mtl_view_use *use;
    doe_mtl_texture_use *texture_use;
    doe_mtl_status status = doe_mtl_use_texture(commands, view->texture, &texture_use);
    if (status != DOE_MTL_OK) return status;
    for (use = commands->views; use != NULL; use = use->next)
        if (use->view == view) return DOE_MTL_OK;
    use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_MTL_OUT_OF_MEMORY;
    status = doe_mtl_texture_view_retain(view);
    if (status != DOE_MTL_OK) {
        free(use);
        return status;
    }
    use->view = view;
    use->next = commands->views;
    commands->views = use;
    return DOE_MTL_OK;
}

static doe_mtl_status use_sampler(doe_mtl_commands *commands, doe_mtl_sampler *sampler) {
    doe_mtl_sampler_use *use;
    doe_mtl_status status;
    for (use = commands->samplers; use != NULL; use = use->next)
        if (use->sampler == sampler) return DOE_MTL_OK;
    use = calloc(1, sizeof(*use));
    if (use == NULL) return DOE_MTL_OUT_OF_MEMORY;
    status = doe_mtl_sampler_retain(sampler);
    if (status != DOE_MTL_OK) {
        free(use);
        return status;
    }
    use->sampler = sampler;
    use->next = commands->samplers;
    commands->samplers = use;
    return DOE_MTL_OK;
}

doe_mtl_status doe_mtl_image_bindings_retain(doe_mtl_commands *commands,
    const doe_mtl_texture_binding *textures, size_t texture_count,
    const doe_mtl_sampler_binding *samplers, size_t sampler_count) {
    size_t i;
    doe_mtl_status status = DOE_MTL_OK;
    for (i = 0; i < texture_count && status == DOE_MTL_OK; ++i) status = doe_mtl_use_view(commands, textures[i].view);
    for (i = 0; i < sampler_count && status == DOE_MTL_OK; ++i) status = use_sampler(commands, samplers[i].sampler);
    return status;
}

void doe_mtl_images_retire(doe_mtl_commands *commands, bool completed, bool success) {
    while (commands->textures != NULL) {
        doe_mtl_texture_use *use = commands->textures;
        commands->textures = use->next;
        if (completed) {
            if (success) memcpy(use->texture->initialized, use->initialized,
                use->texture->subresource_count * sizeof(uint8_t));
            else use->texture->poisoned = true;
        }
        use->texture->owner = NULL;
        doe_mtl_texture_release(use->texture);
        free(use->initialized);
        free(use);
    }
    while (commands->views != NULL) {
        doe_mtl_view_use *use = commands->views;
        commands->views = use->next;
        doe_mtl_texture_view_release(use->view);
        free(use);
    }
    while (commands->samplers != NULL) {
        doe_mtl_sampler_use *use = commands->samplers;
        commands->samplers = use->next;
        doe_mtl_sampler_release(use->sampler);
        free(use);
    }
}
