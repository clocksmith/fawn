/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "stage.h"
#include <string.h>

static bool append_storage(size_t *bytes, size_t count, size_t width, size_t alignment) {
    const size_t padding = (alignment - *bytes % alignment) % alignment;
    if (*bytes > SIZE_MAX - padding) return false;
    *bytes += padding;
    if (count > (SIZE_MAX - *bytes) / width) return false;
    *bytes += count * width;
    return true;
}
bool stage_layout(const DoeCompilerStageCapacities *caps, CompilerLimits *limits,
                  size_t *base, size_t *bytes) {
    if (caps == NULL || caps->interface_attributes >= UINT32_MAX || caps->stage_interfaces >= UINT32_MAX ||
        !compiler_module_layout(&caps->module, limits, base)) return false;
    *bytes = *base;
    return append_storage(bytes, 1u, sizeof(StageWorkspace), _Alignof(StageWorkspace)) &&
        append_storage(bytes, caps->interface_attributes, sizeof(InterfaceAttribute), _Alignof(InterfaceAttribute)) &&
        append_storage(bytes, caps->stage_interfaces, sizeof(StageInterface), _Alignof(StageInterface)) &&
        append_storage(bytes, caps->module.entries, sizeof(StageEntry), _Alignof(StageEntry));
}
/* The checked layout above and these typed records share natural alignments:
 * base is eight-byte aligned, and every following record is a multiple of four. */
void stage_bind(Source *source, void *memory, const DoeCompilerStageCapacities *caps, size_t base) {
    unsigned char *bytes = memory;
    StageWorkspace *stages = (StageWorkspace *)(bytes + base);
    memset(stages, 0, sizeof(*stages));
    stages->attributes = (InterfaceAttribute *)(stages + 1);
    stages->interfaces = (StageInterface *)(stages->attributes + caps->interface_attributes);
    stages->entries = (StageEntry *)(stages->interfaces + caps->stage_interfaces);
    stages->attribute_capacity = (uint32_t)caps->interface_attributes;
    stages->interface_capacity = (uint32_t)caps->stage_interfaces;
    memset(stages->entries, 0, caps->module.entries * sizeof(*stages->entries));
    source->stages = stages;
}
bool stage_graphics(const FunctionRecord *function) {
    return (function->flags & (FUNCTION_VERTEX | FUNCTION_FRAGMENT)) != 0u;
}
uint32_t stage_kind(const FunctionRecord *function) {
    return (function->flags & FUNCTION_VERTEX) != 0u ? DOE_COMPILER_STAGE_VERTEX :
        (function->flags & FUNCTION_FRAGMENT) != 0u ? DOE_COMPILER_STAGE_FRAGMENT : DOE_COMPILER_STAGE_COMPUTE;
}
InterfaceAttribute *stage_attribute(Source *source, uint32_t owner, uint32_t index,
                                    uint32_t offset, bool create) {
    StageWorkspace *stages = source->stages;
    if (stages == NULL) {
        if (create) (void)source_error(source, DOE_COMPILER_UNSUPPORTED, offset,
            "graphics interface attributes require ABI7 interface capacities");
        return NULL;
    }
    for (uint32_t i = 0u; i < stages->attribute_count; ++i)
        if (stages->attributes[i].owner == owner && stages->attributes[i].index == index)
            return &stages->attributes[i];
    if (!create) return NULL;
    if (stages->attribute_count == stages->attribute_capacity) {
        (void)source_pool_error(source, DOE_COMPILER_POOL_INTERFACE_ATTRIBUTES,
            (size_t)stages->attribute_count + 1u, offset, "interface attribute capacity exceeded");
        return NULL;
    }
    InterfaceAttribute *attribute = &stages->attributes[stages->attribute_count++];
    *attribute = (InterfaceAttribute){.owner = owner, .index = index, .offset = offset,
        .location_expression = NO_ID};
    return attribute;
}
uint32_t stage_parameter(const Source *source, NameSpan name) {
    const CompilerWorkspace *c = source->compiler;
    if (source->stages == NULL || c->current_function >= c->function_count ||
        !stage_graphics(&c->functions[c->current_function])) return NO_ID;
    const StageEntry *entry = &source->stages->entries[c->functions[c->current_function].entry_index];
    for (uint32_t i = 0u; i < entry->formal_count; ++i) {
        const uint32_t index = entry->formal_first + i;
        if (source_spans_equal(source, name, c->formals[index].name)) return index;
    }
    return NO_ID;
}
uint64_t stage_name_bytes(const Source *source) {
    uint64_t bytes = 0u;
    if (source->stages != NULL)
        for (uint32_t i = 0u; i < source->stages->interface_count; ++i)
            bytes += (uint64_t)source->stages->interfaces[i].name.length + 1u;
    return bytes;
}
void stage_reflect(const Source *source, DoeCompilerStageInterfaceV7 *outputs, char *names, uint32_t cursor) {
    const CompilerWorkspace *c = source->compiler;
    for (uint32_t i = 0u; i < source->stages->interface_count; ++i) {
        const StageInterface *input = &source->stages->interfaces[i];
        DoeCompilerStageInterfaceV7 *out = &outputs[i];
        memset(out, 0, sizeof(*out));
        out->entry_index = input->entry;
        out->direction = input->direction;
        out->location = input->location;
        out->builtin = input->builtin;
        out->scalar_kind = input->type < 7u ? input->type : c->types[input->type - 7u].element;
        out->lane_count = input->type < 7u ? 1u : c->types[input->type - 7u].count;
        out->interpolation = input->interpolation;
        out->sampling = input->sampling;
        out->flags = input->flags;
        out->name = (DoeCompilerNameV4){cursor, input->name.length};
        memcpy(names + cursor, source->source + input->name.offset, input->name.length);
        cursor += input->name.length;
        names[cursor++] = '\0';
    }
}
bool stage_closure(Source *source, uint32_t function, SourceReference reference) {
    const FunctionRecord *entry = &source->compiler->functions[function];
    if (reference.kind == SOURCE_DISCARD && (entry->flags & FUNCTION_FRAGMENT) == 0u)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, reference.index,
            "discard is restricted to fragment entry call graphs");
    if (!stage_graphics(entry)) return true;
    if (reference.kind == SOURCE_WORKGROUP || reference.kind == SOURCE_BARRIER)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, entry->name.offset,
            "workgroup storage and barriers are restricted to compute entry call graphs");
    if ((entry->flags & FUNCTION_VERTEX) != 0u && reference.kind == SOURCE_SUBGROUP)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, entry->name.offset,
            "subgroup operations are not available in vertex entry call graphs");
    if ((entry->flags & FUNCTION_VERTEX) != 0u && reference.kind == SOURCE_RESOURCE &&
        source->compiler->resources[reference.index].access != DOE_COMPILER_READ)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, entry->name.offset,
            "writable storage resources are unavailable in vertex entry call graphs");
    return true;
}
_Static_assert(sizeof(StageWorkspace) % _Alignof(InterfaceAttribute) == 0u, "stage attribute alignment");
_Static_assert(sizeof(InterfaceAttribute) % _Alignof(StageInterface) == 0u, "stage leaf alignment");
_Static_assert(sizeof(StageInterface) % _Alignof(StageEntry) == 0u, "stage entry alignment");
