/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"

/* One native image-kind predicate drives declaration, access and reflection.
 * WGSL read-only storage images are typed SRVs at relative mip zero. */
bool hlsl_image_uav(const TypeRecord *type) {
    return type->kind == TYPE_TEXTURE && type->state.image.format != 0u &&
        type->state.image.access != DOE_COMPILER_READ;
}
uint32_t hlsl_image_components(HlslEmitter *e, const TypeRecord *t) {
    if ((t->state.image.flags & DOE_COMPILER_HANDLE_DEPTH) != 0u) return 1u;
    switch (t->state.image.format) {
        case 0u: return 4u;
        case DOE_COMPILER_FORMAT_R32FLOAT: case DOE_COMPILER_FORMAT_R32SINT:
        case DOE_COMPILER_FORMAT_R32UINT: return 1u;
        case DOE_COMPILER_FORMAT_RG32FLOAT: case DOE_COMPILER_FORMAT_RG32SINT:
        case DOE_COMPILER_FORMAT_RG32UINT: return 2u;
        case DOE_COMPILER_FORMAT_RGBA32FLOAT: case DOE_COMPILER_FORMAT_RGBA16FLOAT:
        case DOE_COMPILER_FORMAT_RGBA8UNORM: case DOE_COMPILER_FORMAT_RGBA8SNORM:
        case DOE_COMPILER_FORMAT_RGBA32SINT: case DOE_COMPILER_FORMAT_RGBA16SINT:
        case DOE_COMPILER_FORMAT_RGBA8SINT: case DOE_COMPILER_FORMAT_RGBA32UINT:
        case DOE_COMPILER_FORMAT_RGBA16UINT: case DOE_COMPILER_FORMAT_RGBA8UINT: return 4u;
        default: hlsl_error(e, "HLSL storage image format has no exact component mapping"); return 0u;
    }
}
void hlsl_handle_type(HlslEmitter *e, TypeId type) {
    const CompilerWorkspace *c = e->module;
    if (type < IR_PRIMITIVE_COUNT || type - IR_PRIMITIVE_COUNT >= c->type_count) {
        hlsl_error(e, "HLSL handle type identity is invalid");
        return;
    }
    const TypeRecord *t = &c->types[type - IR_PRIMITIVE_COUNT];
    const ImageType image = t->state.image;
    if (t->kind == TYPE_SAMPLER) {
        if (image.flags != 0u && image.flags != DOE_COMPILER_HANDLE_COMPARISON) {
            hlsl_error(e, "HLSL sampler flags have no exact mapping");
            return;
        }
        hlsl_text(e, image.flags == 0u ? "SamplerState" : "SamplerComparisonState");
        return;
    }
    if (t->kind != TYPE_TEXTURE || t->element < IR_I32 || t->element > IR_F32 ||
        (image.flags & ~(uint32_t)(DOE_COMPILER_HANDLE_DEPTH | DOE_COMPILER_HANDLE_MULTISAMPLED | DOE_COMPILER_HANDLE_EXTERNAL)) != 0u) {
        hlsl_error(e, "HLSL texture type or scalar is unsupported");
        return;
    }
    const bool storage = image.format != 0u;
    const bool multisampled = (image.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u;
    const bool depth = (image.flags & DOE_COMPILER_HANDLE_DEPTH) != 0u;
    if ((storage && (multisampled || depth)) || (depth && t->element != IR_F32) ||
        (multisampled && image.dimension != DOE_COMPILER_VIEW_2D)) {
        hlsl_error(e, "HLSL image flags and dimension have no supported combination");
        return;
    }
    const char *dimension = NULL;
    switch (image.dimension) {
        case DOE_COMPILER_VIEW_1D: dimension = "1D"; break;
        case DOE_COMPILER_VIEW_2D:
            dimension = multisampled ? "2DMS" : "2DArray";
            if (!multisampled) e->uses_array_views=true;
            break;
        case DOE_COMPILER_VIEW_2D_ARRAY: dimension = "2DArray"; break;
        case DOE_COMPILER_VIEW_CUBE: dimension = "CubeArray"; e->uses_array_views=true; break;
        case DOE_COMPILER_VIEW_CUBE_ARRAY: dimension = "CubeArray"; break;
        case DOE_COMPILER_VIEW_3D: dimension = "3D"; break;
        default: hlsl_error(e, "HLSL texture dimension is unsupported"); return;
    }
    if (storage && (image.dimension == DOE_COMPILER_VIEW_CUBE || image.dimension == DOE_COMPILER_VIEW_CUBE_ARRAY)) {
        hlsl_error(e, "HLSL storage cube images are unsupported");
        return;
    }
    hlsl_format(e, "%sTexture%s<", hlsl_image_uav(t) ? "RW" : "", dimension);
    hlsl_type(e, t->element);
    const uint32_t count = hlsl_image_components(e, t);
    if (count != 1u) hlsl_format(e, "%u", count);
    hlsl_text(e, ">");
}
void hlsl_handle(HlslEmitter *e, const IrInstruction *ir) {
    const uint32_t resource = ir->operands.scalar.binding;
    if (resource >= e->module->resource_count || e->module->resources[resource].type != ir->type ||
        !type_handle(e->module, ir->type) || !hlsl_entry_resource(e, resource)) {
        hlsl_error(e, "HLSL handle is not a declared resource in this entry closure");
    }
}
void hlsl_image_resource(HlslEmitter *e, uint32_t index) {
    const ResourceRecord *r = &e->module->resources[index];
    const TypeRecord *t = &e->module->types[r->type - IR_PRIMITIVE_COUNT];
    const bool sampler = t->kind == TYPE_SAMPLER;
    const bool storage = t->kind == TYPE_TEXTURE && t->state.image.format != 0u;
    const uint32_t expected = sampler ? DOE_COMPILER_SAMPLER : storage ?
        DOE_COMPILER_STORAGE_TEXTURE : DOE_COMPILER_SAMPLED_TEXTURE;
    if (r->kind != expected) {
        hlsl_error(e, "HLSL handle resource kind does not match its declared type");
        return;
    }
    hlsl_handle_type(e, r->type);
    hlsl_format(e, " r%u : ", index);
    hlsl_register(e, index, sampler ? DOE_COMPILER_HLSL_REGISTER_SAMPLER :
        hlsl_image_uav(t) ? DOE_COMPILER_HLSL_REGISTER_UAV : DOE_COMPILER_HLSL_REGISTER_SRV, 0u);
    hlsl_text(e, ";\n");
}
