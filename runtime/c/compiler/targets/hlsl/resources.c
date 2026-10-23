/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include <inttypes.h>

static bool memory_type(const CompilerWorkspace *c, TypeId type) {
    return (type >= IR_I32 && type <= IR_F32) ||
        (type >= 7u && (c->types[type - 7u].flags & (TYPE_HOST | TYPE_FIXED | TYPE_ABSTRACT)) ==
         (TYPE_HOST | TYPE_FIXED));
}
static void signature(HlslEmitter *e, uint32_t resource, TypeId type, bool store) {
    if (store) hlsl_text(e, "void");
    else hlsl_type(e, type);
    hlsl_format(e, " doe_r%u_%s%u(uint offset", resource, store ? "store" : "load", type);
    if (store) {
        hlsl_text(e, ", ");
        hlsl_type(e, type);
        hlsl_text(e, " value");
    }
    hlsl_text(e, ")");
}
static void word(HlslEmitter *e, uint32_t resource, bool uniform, uint32_t lane) {
    if (uniform) hlsl_format(e, "r%u_data[(offset + %uu) / 16u][((offset + %uu) / 4u) %% 4u]",
                            resource, lane * 4u, lane * 4u);
    else hlsl_format(e, "r%u[(offset + %uu) / 4u]", resource, lane * 4u);
}
static void body(HlslEmitter *e, uint32_t resource, TypeId type, bool store) {
    const CompilerWorkspace *c = e->module;
    const TypeRecord *t = type >= 7u ? &c->types[type - 7u] : NULL;
    const bool uniform = c->resources[resource].kind == DOE_COMPILER_UNIFORM_BUFFER;
    signature(e, resource, type, store);
    hlsl_text(e, " {\n");
    if (!store) {
        hlsl_text(e, "    ");
        hlsl_type(e, type);
        hlsl_text(e, " value = ");
        hlsl_zero(e, type);
        hlsl_text(e, ";\n");
    }
    if (t == NULL || t->kind == TYPE_VECTOR) {
        const TypeId leaf = type_leaf(c, type);
        const uint32_t width = type_vector_width(c, type);
        for (uint32_t lane = 0u; lane < width; ++lane) {
            hlsl_text(e, "    ");
            if (store) word(e, resource, false, lane);
            else {
                hlsl_text(e, "value");
                if (width != 1u) hlsl_format(e, "[%u]", lane);
            }
            hlsl_text(e, " = ");
            if (store) {
                hlsl_text(e, "asuint(value");
                if (width != 1u) hlsl_format(e, "[%u]", lane);
                hlsl_text(e, ")");
            } else {
                hlsl_text(e, leaf == IR_F32 ? "asfloat(" : leaf == IR_I32 ? "asint(" : "uint(");
                word(e, resource, uniform, lane);
                hlsl_text(e, ")");
            }
            hlsl_text(e, ";\n");
        }
    } else if (t->kind == TYPE_ARRAY) {
        hlsl_format(e, "    for (uint i = 0u; i < %uu; ++i) {\n", t->count);
        if (store) hlsl_format(e, "        doe_r%u_store%u(offset + %" PRIu64 "u * i, value.elements[i]);\n",
                               resource, t->element, t->state.layout.stride);
        else hlsl_format(e, "        value.elements[i] = doe_r%u_load%u(offset + %" PRIu64 "u * i);\n",
                          resource, t->element, t->state.layout.stride);
        hlsl_text(e, "    }\n");
    } else if (t->kind == TYPE_STRUCT) {
        for (uint32_t i = 0u; i < t->count && !e->failed; ++i) {
            const MemberRecord *m = &c->members[t->element + i];
            if (store) hlsl_format(e, "    doe_r%u_store%u(offset + %" PRIu64 "u, value.m%u);\n",
                                  resource, m->type, m->offset, i);
            else hlsl_format(e, "    value.m%u = doe_r%u_load%u(offset + %" PRIu64 "u);\n",
                             i, resource, m->type, m->offset);
        }
    } else hlsl_error(e, "HLSL resource helper type has no lowering");
    if (!store) hlsl_text(e, "    return value;\n");
    hlsl_text(e, "}\n");
}
static void declare_resource(HlslEmitter *e, uint32_t index) {
    const ResourceRecord *r = &e->module->resources[index];
    e->offset = r->source_offset;
    if (r->minimum > UINT32_MAX || r->tail_offset > UINT32_MAX || r->tail_stride > UINT32_MAX) {
        hlsl_error(e, "HLSL buffer layout exceeds 32-bit byte addressing");
        return;
    }
    if (r->group == DOE_HLSL_DISPATCH_SPACE && r->binding == DOE_HLSL_DISPATCH_SLOT &&
        r->kind == DOE_COMPILER_UNIFORM_BUFFER &&
        (e->module->entries[e->entry].source_input_mask & (1u << INPUT_NUM_WORKGROUPS)) != 0u) {
        hlsl_error(e, "HLSL uniform collides with dispatch metadata at b0, space7");
        return;
    }
    if (r->kind == DOE_COMPILER_UNIFORM_BUFFER) {
        if (r->minimum == 0u || r->minimum > 65536u) {
            hlsl_error(e, "HLSL constant buffer exceeds the 64 KiB CBV limit");
            return;
        }
        hlsl_format(e, "cbuffer DoeUniform%u : register(b%u, space%u) {\n",
                    index, r->binding, r->group);
        hlsl_format(e, "    uint4 r%u_data[%" PRIu64 "];\n};\n", index, (r->minimum + 15u) / 16u);
    } else if (r->kind == DOE_COMPILER_STORAGE_BUFFER &&
               (r->access == DOE_COMPILER_READ || r->access == DOE_COMPILER_READ_WRITE)) {
        const bool writable = r->access == DOE_COMPILER_READ_WRITE;
        hlsl_format(e, "%sStructuredBuffer<uint> r%u : register(%c%u, space%u);\n",
                    writable ? "RW" : "", index, writable ? 'u' : 't', r->binding, r->group);
        hlsl_format(e, "uint doe_r%u_bytes() {\n    uint count, stride;\n", index);
        hlsl_format(e, "    r%u.GetDimensions(count, stride);\n", index);
        hlsl_text(e, "    return stride == 4u && count <= 0x3fffffffu ? count * 4u : 0u;\n}\n");
    } else hlsl_error(e, "HLSL resource kind or access has no implementation");
}
void hlsl_resources(HlslEmitter *e) {
    const CompilerWorkspace *c = e->module;
    for (uint32_t resource = 0u; resource < c->resource_count && !e->failed; ++resource) {
        if (!hlsl_entry_resource(e, resource)) continue;
        if (type_handle(c, c->resources[resource].type)) {
            hlsl_image_resource(e, resource);
            continue;
        }
        declare_resource(e, resource);
        const bool writable = c->resources[resource].kind == DOE_COMPILER_STORAGE_BUFFER &&
            c->resources[resource].access == DOE_COMPILER_READ_WRITE;
        for (unsigned pass = 0u; pass < 2u; ++pass) {
            for (TypeId type = IR_I32; type < 7u + c->type_count && !e->failed; ++type) {
                if (!memory_type(c, type)) continue;
                if (type_size(c, type) > UINT32_MAX) {
                    hlsl_error(e, "HLSL value layout exceeds 32-bit byte addressing");
                    return;
                }
                for (unsigned mode = 0u; mode < (writable ? 2u : 1u); ++mode) {
                    if (pass == 0u) {
                        signature(e, resource, type, mode != 0u);
                        hlsl_text(e, ";\n");
                    } else body(e, resource, type, mode != 0u);
                }
            }
        }
    }
}
