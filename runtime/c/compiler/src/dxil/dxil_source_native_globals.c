/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_source_native_internal.h"
#include <string.h>

static bool scalar_constant(ConstantValue value, DxilIrConstant *out) {
    out->ty = value.type;
    if (value.type == IR_BOOL) {
        out->kind = DXIL_IR_CONST_BOOL; out->bool_value = value.bits != 0u;
    } else if (value.type == IR_F32 || value.type == IR_F16 || value.type == IR_ABSTRACT_FLOAT) {
        out->kind = DXIL_IR_CONST_FLOAT;
        if (value.type == IR_ABSTRACT_FLOAT) memcpy(&out->float_value, &value.bits, sizeof(out->float_value));
        else if (value.type == IR_F16) out->float_value = numeric_half_value(value.bits);
        else {
            const uint32_t bits = (uint32_t)value.bits;
            float scalar;
            memcpy(&scalar, &bits, sizeof(scalar));
            out->float_value = scalar;
        }
    } else if (value.type == IR_U32 || value.type == IR_I32 || value.type == IR_ABSTRACT_INT) {
        out->kind = DXIL_IR_CONST_INT; out->int_value = value.bits;
    } else return false;
    return true;
}
static bool constant_tree(DxilSourceBridge *b, ConstantValue value, uint32_t *id, unsigned depth) {
    DxilIrModule *module = b->module;
    if (depth > DXIL_IR_MAX_TYPES || module->constant_count == DXIL_IR_MAX_CONSTANTS)
        return dxil_source_native_fail(b, 0u, DXIL_OUTPUT_TOO_LARGE, "native DXIL initializer tree capacity exceeded");
    *id = module->constant_count++;
    DxilIrConstant *out = &module->constants[*id];
    out->ty = dxil_source_native_type(b, value.type);
    if ((value.flags & CONSTANT_AGGREGATE) == 0u) {
        if (!scalar_constant(value, out))
            return dxil_source_native_fail(b, 0u, DXIL_INVALID_IR, "invalid typed scalar initializer");
        return true;
    }
    const uint32_t count = type_children(b->compiler, value.type);
    if (count > DXIL_IR_MAX_CONSTANTS - module->constant_child_count)
        return dxil_source_native_fail(b, 0u, DXIL_OUTPUT_TOO_LARGE, "native DXIL initializer edge capacity exceeded");
    out->composite = true;
    out->children = (DxilIrRange){module->constant_child_count, count};
    module->constant_child_count += count;
    for (uint32_t i = 0u; i < count; ++i) {
        ConstantValue element;
        if (!constant_project(&b->source, value, i, &element) ||
            !constant_tree(b, element, &module->constant_children[out->children.start + i], depth + 1u)) return false;
    }
    return b->status == DXIL_OK;
}
static bool initializer(DxilSourceBridge *b, DxilIrGlobal *global, ConstantValue value) {
    if (!constant_tree(b, value, &global->init_tree, 0u)) return false;
    global->has_initializer = true;
    global->has_init_tree = true;
    const DxilIrConstant *constant = &b->module->constants[global->init_tree];
    if (!constant->composite) {
        global->init_kind = constant->kind; global->init_bool = constant->bool_value;
        global->init_int = constant->int_value; global->init_float = constant->float_value;
    }
    return true;
}
bool dxil_source_native_globals(DxilSourceBridge *b) {
    CompilerWorkspace *c = b->compiler;
    uint32_t count = c->resource_count + c->workgroup_count;
    for (uint32_t i = 0u; i < c->constant_decl_count; ++i)
        if (c->constant_decls[i].expression != NO_ID) ++count;
    if (count > DXIL_IR_MAX_GLOBALS)
        return dxil_source_native_fail(b, 0u, DXIL_OUTPUT_TOO_LARGE, "native DXIL global capacity exceeded");
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        const ResourceRecord *r = &c->resources[i];
        const bool handle = type_handle(c, r->type);
        DxilIrGlobal *g = &b->module->globals[b->module->global_count++];
        *g = (DxilIrGlobal){.name = dxil_source_native_name(b, r->name), .ty = r->type,
            .class = DXIL_IR_GLOBAL_VAR, .has_addr_space = true,
            .addr_space = handle ? DXIL_IR_ADDR_HANDLE : r->kind == DOE_COMPILER_UNIFORM_BUFFER ? DXIL_IR_ADDR_UNIFORM : DXIL_IR_ADDR_STORAGE,
            .has_access = !handle && r->kind != DOE_COMPILER_UNIFORM_BUFFER,
            .access = r->access == DOE_COMPILER_READ_WRITE ? DXIL_IR_ACCESS_READ_WRITE : DXIL_IR_ACCESS_READ,
            .has_binding = true, .binding = {r->group, r->binding}};
    }
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        const WorkgroupRoot *r = &c->workgroups[i];
        if (r->state.resolved.address_space != MODULE_PRIVATE && r->state.resolved.address_space != MODULE_WORKGROUP)
            return dxil_source_native_fail(b, r->name.offset, DXIL_UNSUPPORTED_CONSTRUCT, "module space is outside retained native DXIL IR");
        DxilIrGlobal *g = &b->module->globals[b->module->global_count++];
        *g = (DxilIrGlobal){.name = dxil_source_native_name(b, r->name), .ty = r->type,
            .class = DXIL_IR_GLOBAL_VAR, .has_addr_space = true,
            .addr_space = r->state.resolved.address_space == MODULE_PRIVATE ? DXIL_IR_ADDR_PRIVATE : DXIL_IR_ADDR_WORKGROUP};
        if (r->state.resolved.initializer != NO_ID &&
            !initializer(b, g, c->constant_edges[r->state.resolved.initializer])) return false;
    }
    for (uint32_t i = 0u; i < c->constant_decl_count; ++i) {
        const ConstantSyntax *declaration = &c->constant_decls[i];
        if (declaration->expression == NO_ID) continue;
        DxilIrGlobal *g = &b->module->globals[b->module->global_count++];
        *g = (DxilIrGlobal){.name = dxil_source_native_name(b, declaration->name),
            .ty = declaration->type, .class = DXIL_IR_GLOBAL_CONST};
        if ((declaration->state & DECLARATION_OVERRIDE) != 0u) {
            OverrideRecord *record = override_declaration(&b->source, i);
            if (record == NULL) return dxil_source_native_fail(b, declaration->name.offset, DXIL_INVALID_IR, "missing canonical override record");
            g->class = DXIL_IR_GLOBAL_OVERRIDE;
            g->has_override_id = (record->flags & DOE_COMPILER_OVERRIDE_HAS_ID) != 0u;
            g->override_id = record->id;
        }
        if (declaration->expression != OVERRIDE_NO_INITIALIZER &&
            (declaration->state & DECLARATION_OVERRIDE) != 0u &&
            (declaration->state & DECLARATION_KNOWN) == 0u)
            return dxil_source_native_fail(b, declaration->name.offset, DXIL_UNSUPPORTED_CONSTRUCT,
                "native DXIL override initializer has no resolved default value");
        if (declaration->expression != OVERRIDE_NO_INITIALIZER &&
            !initializer(b, g, (ConstantValue){declaration->type,
                (declaration->state & 16u) != 0u ? CONSTANT_AGGREGATE : 0u, declaration->value.bits})) return false;
    }
    return b->status == DXIL_OK;
}
