#include "draw_indirect_internal.h"

static const char draw_source[] =
    "@group(0) @binding(0) var<storage, read_write> record: array<u32>;\n"
    "@compute @workgroup_size(1) fn main() {\n"
    "    let vertex_count = record[0];\n"
    "    let instance_count = record[1];\n"
    "    let first_vertex = record[2];\n"
    "    let first_instance = record[3];\n"
    "    let vertex_limit = record[4];\n"
    "    let instance_limit = record[5];\n"
    "    if (vertex_count == 0u || instance_count == 0u || first_instance != 0u ||\n"
    "        first_vertex > vertex_limit || vertex_count > vertex_limit - first_vertex ||\n"
    "        instance_count > instance_limit) {\n"
    "        record[0] = 0u;\n        record[1] = 0u;\n"
    "        record[2] = 0u;\n        record[3] = 0u;\n    }\n}\n";

_Static_assert(sizeof(draw_source) <= DOE_VK_DRAW_INDIRECT_SOURCE_CAPACITY, "draw source capacity");

doe_vk_status doe_vk_draw_validator_source(char *out, size_t capacity, size_t *out_length) {
    if (out == NULL || out_length == NULL || capacity < sizeof(draw_source)) return DOE_VK_INVALID_ARGUMENT;
    memcpy(out, draw_source, sizeof(draw_source));
    *out_length = sizeof(draw_source) - 1;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_draw_validator_validate(const doe_vk_caps *caps) {
    const uint32_t one[3] = {1, 1, 1};
    doe_vk_status status = doe_vk_dispatch_validator_validate(caps, one);
    if (status != DOE_VK_OK) return status;
    if (!(caps->queue_flags & VK_QUEUE_GRAPHICS_BIT) || caps->max_storage_buffer_range < 32)
        return DOE_VK_UNSUPPORTED;
    return DOE_VK_OK;
}

doe_vk_status doe_vk_draw_validator_create(doe_vk_context *context,
    const uint32_t *spirv, size_t word_count, doe_vk_draw_validator **out, doe_vk_error *error) {
    if (context == NULL || out == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "draw validator", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "draw validator context", error);
    doe_vk_status status = doe_vk_draw_validator_validate(&context->caps);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "draw validator limits", error);
    doe_vk_draw_validator *validator = calloc(1, sizeof(*validator));
    if (validator == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "draw validator owner", error);
    validator->references = 1;
    const doe_vk_binding_layout layout = {0, 0, DOE_VK_BINDING_STORAGE};
    status = doe_vk_pipeline_create(context, spirv, word_count, "main", &layout, 1, &validator->pipeline, error);
    if (status != DOE_VK_OK) {
        doe_vk_draw_validator_release(validator);
        return status;
    }
    *out = validator;
    return doe_vk_fail(DOE_VK_OK, "draw validator ready", error);
}

doe_vk_status doe_vk_draw_validator_retain(doe_vk_draw_validator *validator) {
    if (validator == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (validator->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++validator->references;
    return DOE_VK_OK;
}
void doe_vk_draw_validator_release(doe_vk_draw_validator *validator) {
    if (validator == NULL || --validator->references != 0) return;
    doe_vk_pipeline_release(validator->pipeline);
    free(validator);
}

doe_vk_status doe_vk_draw_indirect_validate(doe_vk_commands *commands,
    const doe_vk_render_indirect *indirect) {
    if (indirect == NULL || indirect->validator == NULL ||
        indirect->validator->pipeline->context != commands->context || indirect->arguments == NULL ||
        indirect->arguments->context != commands->context || indirect->offset % 4 != 0 ||
        !(indirect->arguments->usage & DOE_VK_BUFFER_INDIRECT) ||
        !(indirect->arguments->usage & DOE_VK_BUFFER_COPY_SRC) ||
        !doe_vk_range(indirect->arguments->size, indirect->offset, 16)) return DOE_VK_INVALID_ARGUMENT;
    return indirect->arguments->content_status;
}

doe_vk_status doe_vk_draw_indirect_prepare(doe_vk_commands *commands,
    const doe_vk_render_indirect *indirect, const uint32_t limits[2], VkBuffer *out) {
    doe_vk_buffer *scratch = NULL;
    doe_vk_status status = doe_vk_buffer_create(commands->context, 32,
        DOE_VK_BUFFER_STORAGE | DOE_VK_BUFFER_COPY_DST | DOE_VK_BUFFER_INDIRECT,
        DOE_VK_MEMORY_HOST, &scratch, NULL);
    if (status != DOE_VK_OK) return status;
    status = doe_vk_buffer_write(scratch, 16, limits, 2 * sizeof(uint32_t));
    if (status == DOE_VK_OK)
        status = doe_vk_commands_copy(commands, indirect->arguments, indirect->offset, scratch, 0, 16);
    const doe_vk_buffer_binding binding = {scratch, 0, 32};
    if (status == DOE_VK_OK)
        status = doe_vk_commands_dispatch(commands, indirect->validator->pipeline, &binding, 1, 1, 1, 1);
    if (status == DOE_VK_OK) {
        doe_vk_barrier(commands);
        *out = scratch->buffer;
    }
    /* Copy/dispatch leases independently retain this distinct scratch record. */
    doe_vk_buffer_release(scratch);
    return status;
}
