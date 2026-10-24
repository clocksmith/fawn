/* SPDX-License-Identifier: MIT */
#include "internal.h"

/* Only the existing command's four use spans share this allocation operation.
 * Exclusive object indices refer to slots, never addresses in movable storage.
 * Failed growth preserves the old allocation and capacity. */
static void *grow_span(void *old, size_t count, size_t *capacity, size_t added, size_t element) {
    size_t maximum = SIZE_MAX / element;
    if (count > maximum || added > maximum - count) return NULL;
    size_t needed = count + added;
    if (needed <= *capacity) return old;
    size_t next = *capacity ? *capacity : 8;
    if (next > maximum) next = needed;
    while (next < needed) next = next > maximum / 2 ? needed : next * 2;
    void *fresh = calloc(next, element);
    if (!fresh) return NULL;
    if (count) memcpy(fresh, old, count * element);
    free(old);
    *capacity = next;
    return fresh;
}

doe_dx_status doe_dx_reserve_uses(doe_dx_commands *commands,
    doe_dx_reservation needed, doe_dx_error *error) {
    doe_dx_buffer_use *buffers = grow_span(commands->buffers, commands->buffer_count,
        &commands->buffer_capacity, needed.buffers, sizeof(*buffers));
    if (!buffers && (commands->buffer_count || needed.buffers))
        return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "commands.reserve.buffers", error);
    commands->buffers = buffers;
    doe_dx_texture_use *textures = grow_span(commands->textures, commands->texture_count,
        &commands->texture_capacity, needed.textures, sizeof(*textures));
    if (!textures && (commands->texture_count || needed.textures))
        return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "commands.reserve.textures", error);
    commands->textures = textures;
    doe_dx_timestamp_use *timestamps = grow_span(commands->timestamps, commands->timestamp_count,
        &commands->timestamp_capacity, needed.timestamps, sizeof(*timestamps));
    if (!timestamps && (commands->timestamp_count || needed.timestamps))
        return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "commands.reserve.timestamps", error);
    commands->timestamps = timestamps;
    doe_dx_pipeline **pipelines = grow_span(commands->pipelines, commands->pipeline_count,
        &commands->pipeline_capacity, needed.pipelines, sizeof(*pipelines));
    if (!pipelines && (commands->pipeline_count || needed.pipelines))
        return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "commands.reserve.pipelines", error);
    commands->pipelines = pipelines;
    return DOE_DX_OK;
}
