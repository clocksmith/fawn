/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "../../../common/include/doe_copy_footprint.h"

bool doe_dx_format_valid(doe_dx_format format) {
    switch (format) {
        case DOE_DX_RGBA8_UNORM:
        case DOE_DX_RGBA8_UINT:
        case DOE_DX_R32_FLOAT:
        case DOE_DX_R32_UINT:
            return true;
    }
    return false;
}

doe_dx_status doe_dx_texture_create(doe_dx_context *context,
    const doe_dx_texture_desc *desc, doe_dx_texture **out, doe_dx_error *error) {
    if (!context || !desc || !out || !desc->width || !desc->height || !desc->usage)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "texture.create", error);
    if (!doe_dx_format_valid(desc->format) || (desc->usage & ~UINT32_C(31)))
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.format_usage", error);
    if ((desc->usage & DOE_DX_TEXTURE_RENDER_ATTACHMENT) && desc->format != DOE_DX_RGBA8_UNORM)
        return doe_dx_fail(DOE_DX_UNSUPPORTED, 0, "texture.render_format", error);
    if (desc->width > 16384 || desc->height > 16384 || context->objects == SIZE_MAX)
        return doe_dx_fail(DOE_DX_RANGE, 0, "texture.extent", error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST, 0, "texture.create", error);
    doe_dx_texture *texture = calloc(1, sizeof(*texture));
    if (!texture) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "texture.allocate", error);
    doe_dx_status status = doe_dx_native_texture_create(context->native, desc, &texture->native, error);
    if (status != DOE_DX_OK) {
        free(texture);
        return doe_dx_context_result(context, status, error);
    }
    texture->context = context;
    texture->desc = *desc;
    texture->references = 1;
    context->objects++;
    *out = texture;
    return doe_dx_fail(DOE_DX_OK, 0, "texture.create", error);
}

doe_dx_status doe_dx_texture_retain(doe_dx_texture *texture) {
    if (!texture) return DOE_DX_INVALID_ARGUMENT;
    if (texture->references == UINT32_MAX) return DOE_DX_RANGE;
    texture->references++;
    return DOE_DX_OK;
}

void doe_dx_texture_release(doe_dx_texture *texture) {
    if (!texture) return;
    texture->references--;
    if (texture->references) return;
    doe_dx_native_texture_destroy(texture->native);
    texture->context->objects--;
    free(texture);
}

doe_dx_status doe_dx_texture_description(const doe_dx_texture *texture, doe_dx_texture_desc *out) {
    if (!texture || !out) return DOE_DX_INVALID_ARGUMENT;
    *out = texture->desc;
    return DOE_DX_OK;
}

doe_dx_status doe_dx_texture_layout_2d(const doe_dx_texture_desc *desc,
    doe_dx_texture_layout layout, doe_dx_texture_footprint *out) {
    if (!desc || !out || !desc->width || !desc->height) return DOE_DX_INVALID_ARGUMENT;
    if (!doe_dx_format_valid(desc->format)) return DOE_DX_UNSUPPORTED;
    if (desc->width > 16384 || desc->height > 16384) return DOE_DX_RANGE;
    if (layout.offset % 512 || layout.row_pitch % 256) return DOE_DX_ALIGNMENT;
    uint32_t row_bytes = desc->width * 4;
    uint32_t pitch = layout.row_pitch ? layout.row_pitch : (row_bytes + 255u) / 256u * 256u;
    if (pitch < row_bytes) return DOE_DX_RANGE;
    uint64_t span;
    if (!doe_copy_footprint(row_bytes, desc->height, desc->height,
        pitch, 1, &span) || span > SIZE_MAX || layout.offset > SIZE_MAX - span)
        return DOE_DX_RANGE;
    *out = (doe_dx_texture_footprint){layout.offset, span, pitch, row_bytes, desc->height};
    return DOE_DX_OK;
}

doe_dx_status doe_dx_texture_copy_footprint(const doe_dx_texture *texture,
    doe_dx_texture_layout layout, doe_dx_texture_footprint *out) {
    if (!texture) return DOE_DX_INVALID_ARGUMENT;
    return doe_dx_texture_layout_2d(&texture->desc, layout, out);
}

doe_dx_texture_use *doe_dx_find_texture(doe_dx_commands *commands, doe_dx_texture *texture) {
    for (size_t index = 0; index < commands->texture_count; index++)
        if (commands->textures[index].texture == texture) return &commands->textures[index];
    return NULL;
}

doe_dx_status doe_dx_texture_preflight(doe_dx_commands *commands,
    doe_dx_texture *texture, uint32_t usage, bool readable) {
    if (!texture) return DOE_DX_INVALID_ARGUMENT;
    if (texture->context != commands->context) return DOE_DX_DEVICE_MISMATCH;
    if (texture->owner && texture->owner != commands) return DOE_DX_BUSY;
    if ((texture->desc.usage & usage) != usage) return DOE_DX_UNSUPPORTED;
    doe_dx_texture_use *use = doe_dx_find_texture(commands, texture);
    if (!use && texture->references == UINT32_MAX) return DOE_DX_RANGE;
    if (readable && !(use ? use->initialized : texture->initialized)) return DOE_DX_UNINITIALIZED;
    return DOE_DX_OK;
}

void doe_dx_use_texture(doe_dx_commands *commands, doe_dx_texture *texture) {
    if (doe_dx_find_texture(commands, texture)) return;
    doe_dx_texture_use *use = &commands->textures[commands->texture_count++];
    use->texture = texture;
    use->initialized = texture->initialized;
    texture->owner = commands;
    texture->references++;
}
