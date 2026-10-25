/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"
#include "../closure.h"
#include <inttypes.h>

void hlsl_register(HlslEmitter *e, uint32_t index, uint32_t kind, uint32_t stride) {
    const ResourceRecord *r = &e->module->resources[index];
    char letter;
    switch (kind) {
        case DOE_COMPILER_HLSL_REGISTER_CBV: letter = 'b'; break;
        case DOE_COMPILER_HLSL_REGISTER_SRV: letter = 't'; break;
        case DOE_COMPILER_HLSL_REGISTER_UAV: letter = 'u'; break;
        case DOE_COMPILER_HLSL_REGISTER_SAMPLER: letter = 's'; break;
        default: hlsl_error(e, "HLSL register namespace is invalid"); return;
    }
    hlsl_format(e, "register(%c%u, space%u)", letter, r->binding, r->group);
    if (e->metadata.bindings != NULL)
        e->metadata.bindings[index] = (DoeCompilerHlslBindingV11){index, kind, r->binding, r->group, stride};
}
/* Half-containing storage uses true byte-addressed native accesses. In
 * particular, a final half never reads or rewrites its adjacent padding. */
bool hlsl_raw_storage(const CompilerWorkspace *c, uint32_t resource) {
    return resource < c->resource_count &&
        c->resources[resource].kind == DOE_COMPILER_STORAGE_BUFFER &&
        (type_flags(c,c->resources[resource].type) & TYPE_F16_CONTENT) != 0u;
}
static bool memory_type(const CompilerWorkspace *c, TypeId type) {
    return ((type >= IR_I32 && type <= IR_F32) || type == IR_F16) ||
        (type >= IR_PRIMITIVE_COUNT && (c->types[type - IR_PRIMITIVE_COUNT].flags & (TYPE_HOST | TYPE_FIXED | TYPE_ABSTRACT)) ==
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
    if (resource >= e->module->resource_count)
        hlsl_format(e, "doe_r%u_word((offset + %uu) / 4u)", resource, lane * 4u);
    else if (uniform) hlsl_format(e, "r%u_data[(offset + %uu) / 16u][((offset + %uu) / 4u) %% 4u]",
                            resource, lane * 4u, lane * 4u);
    else if (hlsl_raw_storage(e->module,resource))
        hlsl_format(e,"r%u.Load(offset + %uu)",resource,lane*4u);
    else hlsl_format(e, "r%u[(offset + %uu) / 4u]", resource, lane * 4u);
}
/* Constant-buffer carriers are padded by the native binding owner. Storage
 * half accesses, unlike CBV carriers, are individually two-byte native ops. */
static void half_lane(HlslEmitter *e, uint32_t resource, bool uniform,
                      uint32_t lane, uint32_t width, bool store) {
    if (!uniform) {
        if (!hlsl_raw_storage(e->module,resource)) {
            hlsl_error(e,"HLSL half storage requires byte-addressed representation"); return;
        }
        e->uses_native16 = true;
        if (store) {
            hlsl_format(e,"    r%u.Store<float16_t>(offset + %uu, value",resource,lane*2u);
            if (width != 1u) hlsl_format(e,"[%u]",lane);
            hlsl_text(e,");\n");
        } else {
            hlsl_text(e,"    value");
            if (width != 1u) hlsl_format(e,"[%u]",lane);
            hlsl_format(e," = r%u.Load<float16_t>(offset + %uu);\n",resource,lane*2u);
        }
        return;
    }
    hlsl_format(e,"    { uint byte = offset + %uu; uint shift = (byte & 2u) * 8u;\n",lane*2u);
    hlsl_text(e,"      value");
    if (width != 1u) hlsl_format(e,"[%u]",lane);
    hlsl_text(e," = float16_t(f16tof32((");
    if (resource >= e->module->resource_count) hlsl_format(e,"doe_r%u_word(byte / 4u)",resource);
    else hlsl_format(e,"r%u_data[byte / 16u][(byte / 4u) %% 4u]",resource);
    hlsl_text(e," >> shift) & 0xffffu));\n    }\n");
}
static void body(HlslEmitter *e, uint32_t resource, TypeId type, bool store) {
    const CompilerWorkspace *c = e->module;
    const TypeRecord *t = type >= IR_PRIMITIVE_COUNT ? &c->types[type - IR_PRIMITIVE_COUNT] : NULL;
    const bool uniform = resource >= c->resource_count || c->resources[resource].kind == DOE_COMPILER_UNIFORM_BUFFER;
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
            if (leaf == IR_F16) {
                half_lane(e, resource, uniform, lane, width, store);
                continue;
            }
            hlsl_text(e, "    ");
            if (store && hlsl_raw_storage(c,resource))
                hlsl_format(e,"r%u.Store(offset + %uu, ",resource,lane*4u);
            else if (store) word(e, resource, false, lane);
            else {
                hlsl_text(e, "value");
                if (width != 1u) hlsl_format(e, "[%u]", lane);
            }
            if (!store || !hlsl_raw_storage(c,resource)) hlsl_text(e, " = ");
            if (store) {
                hlsl_text(e, "asuint(value");
                if (width != 1u) hlsl_format(e, "[%u]", lane);
                hlsl_text(e, ")");
            } else {
                hlsl_text(e, leaf == IR_F32 ? "asfloat(" : leaf == IR_I32 ? "asint(" : "uint(");
                word(e, resource, uniform, lane);
                hlsl_text(e, ")");
            }
            if (store && hlsl_raw_storage(c,resource)) hlsl_text(e,")");
            hlsl_text(e, ";\n");
        }
    } else if (t->kind == TYPE_ARRAY || t->kind == TYPE_MATRIX) {
        const char *member = t->kind == TYPE_ARRAY ? ".elements" : "";
        hlsl_format(e, "    for (uint i = 0u; i < %uu; ++i) {\n", t->count);
        if (store) hlsl_format(e, "        doe_r%u_store%u(offset + %" PRIu64 "u * i, value%s[i]);\n",
                               resource, t->element, t->state.layout.stride, member);
        else hlsl_format(e, "        value%s[i] = doe_r%u_load%u(offset + %" PRIu64 "u * i);\n",
                          member, resource, t->element, t->state.layout.stride);
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
        hlsl_format(e, "cbuffer DoeUniform%u : ", index);
        hlsl_register(e, index, DOE_COMPILER_HLSL_REGISTER_CBV, 0u);
        hlsl_text(e, " {\n");
        hlsl_format(e, "    uint4 r%u_data[%" PRIu64 "];\n};\n", index, (r->minimum + 15u) / 16u);
    } else if (r->kind == DOE_COMPILER_STORAGE_BUFFER &&
               (r->access == DOE_COMPILER_READ || r->access == DOE_COMPILER_READ_WRITE)) {
        const bool writable = r->access == DOE_COMPILER_READ_WRITE;
        if (hlsl_raw_storage(e->module,index)) {
            e->uses_raw_storage = true;
            e->uses_native16 = true;
            hlsl_format(e,"%sByteAddressBuffer r%u : ",writable ? "RW" : "",index);
            hlsl_register(e,index,writable ? DOE_COMPILER_HLSL_REGISTER_UAV : DOE_COMPILER_HLSL_REGISTER_SRV,0u);
            hlsl_format(e,";\nuint doe_r%u_bytes() { return doe_storage_sizes[%uu][%uu]; }\n",index,index/4u,index%4u);
        } else {
            hlsl_format(e, "%sStructuredBuffer<uint> r%u : ", writable ? "RW" : "", index);
            hlsl_register(e, index, writable ? DOE_COMPILER_HLSL_REGISTER_UAV : DOE_COMPILER_HLSL_REGISTER_SRV, 4u);
            hlsl_text(e, ";\n");
            hlsl_format(e, "uint doe_r%u_bytes() {\n    uint count, stride;\n", index);
            hlsl_format(e, "    r%u.GetDimensions(count, stride);\n", index);
            hlsl_text(e, "    return stride == 4u && count <= 0x3fffffffu ? count * 4u : 0u;\n}\n");
        }
    } else hlsl_error(e, "HLSL resource kind or access has no implementation");
}
static void immediate_resource(HlslEmitter *e) {
    const CompilerWorkspace *c = e->module;
    const uint32_t root = target_immediate_root(c,&c->entries[e->entry]);
    if (root == NO_ID) return;
    const uint64_t bytes = type_size(c,c->workgroups[root].type);
    if (bytes > 65536u) { hlsl_error(e,"HLSL immediate block exceeds CBV capacity"); return; }
    const uint32_t key = c->resource_count + root;
    const uint32_t words = (uint32_t)((bytes+3u)/4u);
    /* Root constants cannot be dynamically indexed arrays. Flatten the small
     * immediate block and select named scalar fields; the existing typed load
     * and access guards keep byte layout, half extraction and OOB semantics. */
    hlsl_format(e,"cbuffer DoeImmediate : register(b%u, space%u) {\n",
        DOE_COMPILER_HLSL_IMMEDIATE_REGISTER,DOE_COMPILER_HLSL_IMMEDIATE_SPACE);
    for (uint32_t i = 0u; i < words && !e->failed; ++i)
        hlsl_format(e,"    uint r%u_word%u;\n",key,i);
    hlsl_text(e,"};\n");
    hlsl_format(e,"uint doe_r%u_word(uint index) {\n",key);
    for (uint32_t i = 0u; i < words && !e->failed; ++i)
        hlsl_format(e,"    if (index == %uu) return r%u_word%u;\n",i,key,i);
    hlsl_text(e,"    return 0u;\n}\n");
    for (unsigned pass = 0u; pass < 2u; ++pass) {
        for (TypeId type = IR_I32; type < IR_PRIMITIVE_COUNT+c->type_count && !e->failed; ++type) {
            if (!memory_type(c,type)) continue;
            const char *error = NULL;
            const bool used = target_access_helper_used(c,&e->closure,ROOT_WORKGROUP,root,type,false,&e->traversal_left,&error);
            if (error != NULL) { hlsl_error(e,error); return; }
            if (!used) continue;
            if (pass == 0u) { signature(e,key,type,false); hlsl_text(e,";\n"); }
            else body(e,key,type,false);
        }
    }
}
void hlsl_resources(HlslEmitter *e) {
    immediate_resource(e);
    const CompilerWorkspace *c = e->module;
    bool raw = false;
    for (uint32_t i = 0u; i < c->resource_count; ++i)
        if (hlsl_entry_resource(e,i) && hlsl_raw_storage(c,i)) raw = true;
    if (raw) {
        if (c->resource_count > 16384u) {
            hlsl_error(e,"HLSL logical storage-size table exceeds 64 KiB CBV capacity"); return;
        }
        hlsl_format(e,"cbuffer DoeStorageSizes : register(b%u, space%u) { uint4 doe_storage_sizes[%u]; };\n",
            DOE_COMPILER_HLSL_STORAGE_SIZES_REGISTER,DOE_COMPILER_HLSL_STORAGE_SIZES_SPACE,
            (c->resource_count+3u)/4u);
    }
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
            for (TypeId type = IR_I32; type < IR_PRIMITIVE_COUNT + c->type_count && !e->failed; ++type) {
                if (!memory_type(c, type)) continue;
                for (unsigned mode = 0u; mode < (writable ? 2u : 1u); ++mode) {
                    const char *error = NULL;
                    const bool used = target_access_helper_used(c, &e->closure, ROOT_RESOURCE,
                        resource, type, mode != 0u, &e->traversal_left, &error);
                    if (error != NULL) { hlsl_error(e, error); return; }
                    if (!used) continue;
                    if (type_size(c, type) > UINT32_MAX) {
                        hlsl_error(e, "HLSL value layout exceeds 32-bit byte addressing");
                        return;
                    }
                    if (pass == 0u) {
                        signature(e, resource, type, mode != 0u);
                        hlsl_text(e, ";\n");
                    } else body(e, resource, type, mode != 0u);
                }
            }
        }
    }
}
