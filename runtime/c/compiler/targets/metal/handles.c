/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"

static bool storage_format(const TypeRecord *t) {
    TypeId scalar;
    switch (t->state.image.format) {
        case DOE_COMPILER_FORMAT_RGBA32FLOAT: case DOE_COMPILER_FORMAT_RGBA16FLOAT:
        case DOE_COMPILER_FORMAT_R32FLOAT: case DOE_COMPILER_FORMAT_RGBA8UNORM:
        case DOE_COMPILER_FORMAT_RGBA8SNORM: case DOE_COMPILER_FORMAT_RG32FLOAT:
            scalar = IR_F32;
            break;
        case DOE_COMPILER_FORMAT_RGBA32SINT: case DOE_COMPILER_FORMAT_RGBA16SINT:
        case DOE_COMPILER_FORMAT_RGBA8SINT: case DOE_COMPILER_FORMAT_R32SINT:
        case DOE_COMPILER_FORMAT_RG32SINT:
            scalar = IR_I32;
            break;
        case DOE_COMPILER_FORMAT_RGBA32UINT: case DOE_COMPILER_FORMAT_RGBA16UINT:
        case DOE_COMPILER_FORMAT_RGBA8UINT: case DOE_COMPILER_FORMAT_R32UINT:
        case DOE_COMPILER_FORMAT_RG32UINT:
            scalar = IR_U32;
            break;
        default: return false;
    }
    return scalar == t->element;
}
void metal_handle_type(MetalEmitter *e, TypeId type) {
    const CompilerWorkspace *c = e->module;
    if (type < 7u || type - 7u >= c->type_count) {
        metal_error(e, "Metal handle type identity is invalid");
        return;
    }
    const TypeRecord *t = &c->types[type - 7u];
    const ImageType image = t->state.image;
    if (t->kind == TYPE_SAMPLER) {
        if (image.flags != 0u && image.flags != DOE_COMPILER_HANDLE_COMPARISON) {
            metal_error(e, "Metal sampler flags have no supported mapping");
            return;
        }
        metal_text(e, "sampler");
        return;
    }
    const bool depth = (image.flags & DOE_COMPILER_HANDLE_DEPTH) != 0u;
    const bool multisampled = (image.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u;
    const bool storage = image.format != 0u;
    if (t->kind != TYPE_TEXTURE || t->element < IR_I32 || t->element > IR_F32 ||
        (image.flags & ~(uint32_t)(DOE_COMPILER_HANDLE_DEPTH | DOE_COMPILER_HANDLE_MULTISAMPLED)) != 0u ||
        (depth && t->element != IR_F32) || (storage && (depth || multisampled || !storage_format(t))) ||
        (multisampled && image.dimension != DOE_COMPILER_VIEW_2D)) {
        metal_error(e, "Metal texture flags, scalar or storage format have no supported mapping");
        return;
    }
    const char *dimension;
    switch (image.dimension) {
        case DOE_COMPILER_VIEW_1D: dimension = "1d"; break;
        case DOE_COMPILER_VIEW_2D: dimension = multisampled ? "2d_ms" : "2d"; break;
        case DOE_COMPILER_VIEW_2D_ARRAY: dimension = "2d_array"; break;
        case DOE_COMPILER_VIEW_CUBE: dimension = "cube"; break;
        case DOE_COMPILER_VIEW_CUBE_ARRAY: dimension = "cube_array"; break;
        case DOE_COMPILER_VIEW_3D: dimension = "3d"; break;
        default: metal_error(e, "Metal texture dimension is invalid"); return;
    }
    if ((depth && (image.dimension == DOE_COMPILER_VIEW_1D || image.dimension == DOE_COMPILER_VIEW_3D)) ||
        (storage && (image.dimension == DOE_COMPILER_VIEW_CUBE || image.dimension == DOE_COMPILER_VIEW_CUBE_ARRAY))) {
        metal_error(e, "Metal texture dimension and access have no supported combination");
        return;
    }
    const char *access = multisampled ? "read" : "sample";
    if (storage) {
        switch (image.access) {
            case DOE_COMPILER_READ: access = "read"; break;
            case DOE_COMPILER_WRITE: access = "write"; break;
            case DOE_COMPILER_READ_WRITE: access = "read_write"; break;
            default: metal_error(e, "Metal storage texture access is invalid"); return;
        }
    } else if (image.access != DOE_COMPILER_READ) {
        metal_error(e, "Metal sampled texture access must be read-only");
        return;
    }
    metal_format(e, "%s%s<", depth ? "depth" : "texture", dimension);
    metal_type(e, t->element);
    metal_format(e, ", access::%s>", access);
}
void metal_handle(MetalEmitter *e, const IrInstruction *ir) {
    const uint32_t resource = ir->operands.scalar.binding;
    if (ir->result == 0u || ir->result > e->module->instruction_count ||
        &e->module->instructions[ir->result - 1u] != ir || ir->type < 7u || ir->type - 7u >= e->module->type_count ||
        !type_handle(e->module, ir->type) || resource >= e->module->resource_count ||
        e->module->resources[resource].type != ir->type) {
        metal_error(e, "Metal handle does not identify its declared typed resource");
    }
}
bool metal_resource_slot(MetalEmitter *e, const ResourceRecord *r, uint32_t *slot) {
    if (e->metadata != NULL) {
        const uint32_t resource = (uint32_t)(r - e->module->resources);
        const DoeCompilerMetalBindingV12 *binding = metal_dense_binding(e, e->current_entry, resource);
        if (binding == NULL) return false;
        *slot = binding->slot;
        return true;
    }
    uint32_t table;
    if (!metal_resource_table(e, r, &table)) return false;
    if (table == DOE_COMPILER_TEXT_TABLE_BUFFER) {
        if (doe_metal_binding_slot(r->group, r->binding, slot)) return true;
        metal_error(e, "Metal buffer binding exceeds slots 0 through 29 (slot 30 is sizes)");
        return false;
    }
    const uint32_t limit = table == DOE_COMPILER_TEXT_TABLE_SAMPLER ? DOE_METAL_SAMPLER_SLOTS : DOE_METAL_TEXTURE_SLOTS;
    if (r->binding >= DOE_METAL_BINDINGS_PER_GROUP ||
        r->group > (limit - 1u) / DOE_METAL_BINDINGS_PER_GROUP ||
        r->group * DOE_METAL_BINDINGS_PER_GROUP + r->binding >= limit) {
        metal_error(e, table == DOE_COMPILER_TEXT_TABLE_SAMPLER ?
            "Metal sampler binding exceeds slots 0 through 15" : "Metal texture binding exceeds slots 0 through 30");
        return false;
    }
    *slot = r->group * DOE_METAL_BINDINGS_PER_GROUP + r->binding;
    return true;
}
const char *metal_buffer_qualifier(const ResourceRecord *r) {
    if (r->kind == DOE_COMPILER_UNIFORM_BUFFER) return "constant";
    return r->access == DOE_COMPILER_READ_WRITE ? "device" : "device const";
}
void metal_resource_parameter(MetalEmitter *e, uint32_t index, bool attribute) {
    const ResourceRecord *r = &e->module->resources[index];
    uint32_t slot;
    if (attribute && !metal_resource_slot(e, r, &slot)) return;
    const char *table;
    if (r->kind == DOE_COMPILER_UNIFORM_BUFFER || r->kind == DOE_COMPILER_STORAGE_BUFFER) {
        metal_format(e, "%s uchar*", metal_buffer_qualifier(r));
        table = "buffer";
    } else {
        metal_handle_type(e, r->type);
        table = r->kind == DOE_COMPILER_SAMPLER ? "sampler" : "texture";
    }
    metal_format(e, " r%u", index);
    if (attribute) metal_format(e, " [[%s(%u)]]", table, slot);
}
/* MSL forbids local writable texture variables. Pass the already resolved
 * global handles through ordinary function parameters, not mutable context
 * fields or nullable copies. This walk reads only typed IR_CALL/IR_HANDLE edges.
 * Functions without entry callers still get valid signatures. */
static bool function_resource(MetalEmitter *e, uint32_t function, uint32_t resource) {
    struct { uint32_t function, cursor; } stack[DOE_METAL_MAX_DEPTH];
    const CompilerWorkspace *c = e->module;
    if (function >= c->function_count) {
        metal_error(e, "Metal handle closure function identity is invalid");
        return false;
    }
    unsigned depth = 0u;
    stack[0].function = function;
    stack[0].cursor = c->functions[function].instruction_start;
    for (;;) {
        if (e->handle_work_left == 0u) {
            metal_error(e, "Metal handle closure exceeds its composed traversal budget");
            return false;
        }
        --e->handle_work_left;
        const FunctionRecord *f = &c->functions[stack[depth].function];
        if (f->instruction_start > f->instruction_end || f->instruction_end > c->instruction_count) {
            metal_error(e, "Metal handle closure instruction range is invalid");
            return false;
        }
        if (stack[depth].cursor == f->instruction_end) {
            if (depth == 0u) return false;
            --depth;
            continue;
        }
        const IrInstruction *ir = &c->instructions[stack[depth].cursor++];
        if (ir->opcode == IR_HANDLE && ir->operands.scalar.binding == resource) return true;
        if (ir->opcode != IR_CALL) continue;
        const uint32_t callee = ir->operands.scalar.binding;
        if (callee >= c->function_count || depth + 1u == DOE_METAL_MAX_DEPTH) {
            metal_error(e, "Metal handle closure call identity or depth is invalid");
            return false;
        }
        for (unsigned i = 0u; i <= depth; ++i) {
            if (stack[i].function == callee) {
                metal_error(e, "Metal handle closure contains a recursive call");
                return false;
            }
        }
        ++depth;
        stack[depth].function = callee;
        stack[depth].cursor = c->functions[callee].instruction_start;
    }
}
void metal_handle_arguments(MetalEmitter *e, uint32_t function, bool declaration) {
    for (uint32_t i = 0u; i < e->module->resource_count && !e->failed; ++i) {
        if (!type_handle(e->module, e->module->resources[i].type) || !function_resource(e, function, i)) continue;
        metal_text(e, ", ");
        if (declaration) {
            metal_handle_type(e, e->module->resources[i].type);
            metal_text(e, " ");
        }
        metal_format(e, "r%u", i);
    }
}
