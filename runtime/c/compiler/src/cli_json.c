/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "cli_json.h"
#include <stdarg.h>
#include <stdio.h>

typedef struct { char *bytes; size_t capacity, length; } Json;
static bool append(Json *json, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(json->bytes + json->length, json->capacity - json->length,
                                  format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= json->capacity - json->length) return false;
    json->length += (size_t)written;
    return true;
}
static const char *scalar_name(uint32_t scalar) {
    return scalar == DOE_COMPILER_F32 ? "f32" : "u32";
}
bool cli_json_v1(const DoeCompilerReflection *r, char *bytes, size_t capacity, size_t *length) {
    Json json = { .bytes = bytes, .capacity = capacity };
    if (!append(&json, "{\n  \"entry_point\": \"%s\",\n"
        "  \"workgroup_size\": [%u, %u, %u],\n"
        "  \"binding_count\": %u,\n  \"bindings\": [\n",
        r->entry_point, r->workgroup_size[0], r->workgroup_size[1],
        r->workgroup_size[2], r->binding_count)) return false;
    for (uint32_t i = 0; i < r->binding_count; ++i) {
        const DoeCompilerBinding *binding = &r->bindings[i];
        if (!append(&json,
            "    {\"name\": \"%s\", \"group\": %u, \"binding\": %u, "
            "\"scalar_type\": \"%s\", \"access\": \"%s\", "
            "\"element_stride\": %u, \"runtime_sized\": %s}%s\n",
            binding->name, binding->group, binding->binding,
            scalar_name((uint32_t)binding->scalar),
            binding->access == DOE_COMPILER_READ ? "read" : "read_write",
            binding->element_stride, binding->runtime_sized != 0u ? "true" : "false",
            i + 1u == r->binding_count ? "" : ",")) return false;
    }
    if (!append(&json, "  ]\n}\n")) return false;
    *length = json.length;
    return true;
}
bool cli_json_v2(const DoeCompilerReflectionV2 *r, char *bytes, size_t capacity, size_t *length) {
    Json json = { .bytes = bytes, .capacity = capacity };
    if (!append(&json, "{\n  \"schema_version\": 2,\n  \"entry_point\": \"%s\",\n"
        "  \"workgroup_size\": [%u, %u, %u],\n  \"binding_count\": %u,\n  \"bindings\": [\n",
        r->entry_point, r->workgroup_size[0], r->workgroup_size[1],
        r->workgroup_size[2], r->binding_count)) return false;
    for (uint32_t i = 0; i < r->binding_count; ++i) {
        const DoeCompilerBindingV2 *binding = &r->bindings[i];
        const bool uniform = binding->kind == DOE_COMPILER_UNIFORM_BUFFER;
        if (!append(&json,
            "    {\"name\": \"%s\", \"group\": %u, \"binding\": %u, "
            "\"kind\": \"%s\", \"access\": \"%s\", \"scalar\": ",
            binding->name, binding->group, binding->binding,
            uniform ? "uniform_buffer" : "storage_buffer",
            binding->access == DOE_COMPILER_READ ? "read" : "read_write")) return false;
        if (uniform) { if (!append(&json, "null")) return false; }
        else if (!append(&json, "\"%s\"", scalar_name(binding->scalar))) return false;
        if (!append(&json,
            ", \"element_stride\": %u, \"runtime_sized\": %u, "
            "\"minimum_binding_size_bytes\": %u, \"type_alignment_bytes\": %u, "
            "\"address_space_alignment_bytes\": %u, \"struct_index\": ",
            binding->element_stride, binding->runtime_sized, binding->minimum_binding_size_bytes,
            binding->type_alignment_bytes, binding->address_space_alignment_bytes)) return false;
        if (uniform) { if (!append(&json, "%u", binding->struct_index)) return false; }
        else if (!append(&json, "null")) return false;
        if (!append(&json, "}%s\n", i + 1u == r->binding_count ? "" : ",")) return false;
    }
    if (!append(&json, "  ],\n  \"struct_count\": %u,\n  \"structs\": [\n", r->struct_count)) return false;
    for (uint32_t i = 0; i < r->struct_count; ++i) {
        const DoeCompilerStructV2 *type = &r->structs[i];
        if (!append(&json,
            "    {\"name\": \"%s\", \"member_count\": %u, \"size_bytes\": %u, "
            "\"alignment_bytes\": %u, \"members\": [\n",
            type->name, type->member_count, type->size_bytes, type->alignment_bytes)) return false;
        for (uint32_t j = 0; j < type->member_count; ++j) {
            const DoeCompilerMemberV2 *member = &type->members[j];
            if (!append(&json,
                "      {\"name\": \"%s\", \"scalar\": \"%s\", \"offset_bytes\": %u, "
                "\"size_bytes\": %u, \"alignment_bytes\": %u}%s\n",
                member->name, scalar_name(member->scalar), member->offset_bytes,
                member->size_bytes, member->alignment_bytes,
                j + 1u == type->member_count ? "" : ",")) return false;
        }
        if (!append(&json, "    ]}%s\n", i + 1u == r->struct_count ? "" : ",")) return false;
    }
    if (!append(&json, "  ]\n}\n")) return false;
    *length = json.length;
    return true;
}
